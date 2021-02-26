#include "DebugManager.h"
#include "CompilerConfiguration.h"
#include "BuildDriver.h"
#include "SysUtils.h"
#include "PathUtils.h"
#include "CobolUtils.h"
#include "UiUtils.h"
#include "Ide.h"
#include "MetadataManager.h"
#include "StdStreamRedirect.h"
#include "OutputWindow.h"
#include "GixDebugger.h"

#if defined(_WIN32) || defined(_WIN64)
#include "windows.h"
#endif

#include <QThread>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QMouseEvent>
#include <QMetaEnum>
#include <ServerConfig.h>

#define GET_VARS_LEN_SIZE	5

DebugManager::DebugManager(IdeTaskManager *_ide_task_manager)
{
	debug_driver = nullptr;
	ide_task_manager = _ide_task_manager;
	is_debugging_enabled = true;
	is_user_initiated_stop = false;
	cur_line = 0;
	debugged_prj = nullptr;
}


DebugManager::~DebugManager()
{
	ide_task_manager->logMessage(GIX_CONSOLE_LOG, "Stopping debug driver", QLogger::LogLevel::Debug);
	if (debug_driver) {
		debug_driver->stop();
	}

	for (ModuleDebugInfo *mdi : modules) {
		delete mdi;
	}

	if (!tmp_cfg_path.isEmpty() && QFile::exists(tmp_cfg_path))
		QFile::remove(tmp_cfg_path);

	saveDebugManagerState();
}

void DebugManager::setDebuggingEnabled(bool f)
{
	is_debugging_enabled = f;
}

bool DebugManager::start(Project *prj, QString build_configuration, QString target_platform)
{
	QSettings settings;

	debugged_prj = prj;

	bool dbg_stop_at_first_line = prj->PropertyGetValue("dbg_stop_at_first_line").toBool();
	bool dbg_separate_console = prj->PropertyGetValue("dbg_separate_console").toBool();

	CompilerConfiguration *compiler_cfg = CompilerConfiguration::get(build_configuration, target_platform);
	if (compiler_cfg == nullptr) {
		ide_task_manager->logMessage(GIX_CONSOLE_LOG, "Invalid compiler configuration", QLogger::LogLevel::Error);
		return false;
	}

	QMap<QString, QVariant> build_env;
	build_env.insert("configuration", build_configuration);
	build_env.insert("platform", target_platform);
	build_env.insert("sys.objext", SysUtils::isWindows() && compiler_cfg->isVsBased ? ".obj" : ".o");
	build_env.insert("sys.dllext", SysUtils::isWindows() ? ".dll" : ".so");
	build_env.insert("sys.exeext", SysUtils::isWindows() ? ".exe" : "");
	build_env.insert("prj.build_dir", "${prj.basedir}/bin/${configuration}/${platform}");

	for (auto it = prj->getRuntimeProperties().begin(); it != prj->getRuntimeProperties().end(); ++it) {
		build_env.insert(it.key(), it.value());
	}

	for (auto it = prj->PropertyGetCurrentValues()->begin(); it != prj->PropertyGetCurrentValues()->end(); ++it) {
		build_env.insert(it.key(), it.value());
	}

	MacroManager mm(build_env);

#if _DEBUG
	BuildTarget *bt = prj->getBuildTarget(build_env, nullptr);
#else
	QScopedPointer<BuildTarget> bt(prj->getBuildTarget(build_env, nullptr));
#endif
	QString target = bt->filename();
	QString module_name = PathUtils::toModuleName(target);

	QString startup_item_name = prj->getStartupItemName();
	if (!startup_item_name.isEmpty()) {
		module_name = startup_item_name;
	}
	else {
		if (prj->getType() == ProjectType::MultipleBinaries) {
			QString msg = tr("Multi-binaries projects need a startup item to be set in order to be run or debugged");
			ide_task_manager->logMessage(GIX_CONSOLE_LOG, msg, QLogger::LogLevel::Error);
			UiUtils::ErrorDialog(msg);
			return false;
		}
	}

	bool dbg_merge_env = prj->PropertyGetValue("dbg_merge_env").toBool();

	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	SysUtils::mergeEnvironmentVariable(env, "PATH", compiler_cfg->binDirPath);
	SysUtils::mergeEnvironmentVariable(env, "PATH", compiler_cfg->libDirPath);
	SysUtils::mergeEnvironmentVariable(env, "PATH", GixGlobals::getGixLibDir(target_platform));

	env.insert("COB_EXIT_WAIT", "Y");

	if (prj->isEsql()) {
		QString esql_default_driver = prj->PropertyGetValue("esql_default_driver").toString();
		if (esql_default_driver.isEmpty()) {
			ide_task_manager->logMessage(GIX_CONSOLE_LOG, tr("ESQL default driver not set"), QLogger::LogLevel::Error);
			return false;
		}

		if (esql_default_driver == "_GIXSQL_DB_MODE") {
			QString db_mode = qgetenv("GIXSQL_DB_MODE");
			if (db_mode.isEmpty()) {
				ide_task_manager->logMessage(GIX_CONSOLE_LOG, QString(tr("Invalid value for ESQL default driver in GIXSQL_DB_MODE: %1")).arg(db_mode), QLogger::LogLevel::Error);
				return false;
			}
		}
		else
			if (esql_default_driver == "_URL") {
				// Do nothing
			}
			else
				if (esql_default_driver == "ODBC") {
					env.insert("GIXSQL_DB_MODE", "ODBC");
					ide_task_manager->logMessage(GIX_CONSOLE_LOG, QString(tr("Setting ESQL driver in GIXSQL_DB_MODE: %1")).arg("ODBC"), QLogger::LogLevel::Trace);
				}
				else
					if (esql_default_driver == "MYSQL") {
						env.insert("GIXSQL_DB_MODE", "MYSQL");
						ide_task_manager->logMessage(GIX_CONSOLE_LOG, QString(tr("Setting ESQL driver in GIXSQL_DB_MODE: %1")).arg("MYSQL"), QLogger::LogLevel::Trace);
					}
					else
						if (esql_default_driver == "PGSQL") {
							env.insert("GIXSQL_DB_MODE", "PGSQL");
							ide_task_manager->logMessage(GIX_CONSOLE_LOG, QString(tr("Setting ESQL driver in GIXSQL_DB_MODE: %1")).arg("PGSQL"), QLogger::LogLevel::Trace);
						}
						else {
							ide_task_manager->logMessage(GIX_CONSOLE_LOG, QString(tr("Invalid value for ESQL default driver: %1")).arg(esql_default_driver), QLogger::LogLevel::Error);
							return false;
						}
	}



	QString build_dir = prj->getBuildDirectory(build_configuration, target_platform);
	QString working_dir = (prj->PropertyGetValue("dbg_working_dir").toString().isEmpty()) ? build_dir : prj->PropertyGetValue("dbg_working_dir").toString();

	QString output_path = prj->PropertyGetValue("output_path").toString();
	if (output_path.isEmpty())
		output_path = build_dir;

	mm.add("prj.output_path", output_path);

	QString target_full_path = mm.translate(target);

	if (!QFile::exists(target_full_path)) {
		ide_task_manager->logMessage(GIX_CONSOLE_LOG, "Invalid target path " + target_full_path, QLogger::LogLevel::Error);
		return false;
	}

	SysUtils::mergeEnvironmentVariable(env, "COB_LIBRARY_PATH", output_path);

	if (SysUtils::isLinux()) {
		SysUtils::mergeEnvironmentVariable(env, "LD_LIBRARY_PATH", compiler_cfg->libDirPath);
	}

	QStringList dbg_run_env_vars = prj->PropertyGetValue("dbg_run_env_vars").toStringList();
	for (QString var : dbg_run_env_vars) {
		if (!var.contains("="))
			continue;

		QString name = var.left(var.indexOf("="));
		QString value = var.mid(var.indexOf("=") + 1);
		if (dbg_merge_env)
			SysUtils::mergeEnvironmentVariable(env, name, value);
		else
			env.insert(name, value);
	}

	// ******************************************************

	QString cmd;
	QStringList cobcrun_opts;
	bool uses_external_cmd = !(prj->PropertyGetValue("dbg_cmd").toString().isEmpty());

	QString build_type = prj->PropertyGetValue("build_type").toString();

	// No need to manually delete these, the ServerConfig they're eventually attached to 
	// will do it for us when it goes out of scope
	ServiceConfig *svc_rest = nullptr, *svc_soap = nullptr;

	ProjectFile *main_module_obj = prj->getStartupItem();

	if (main_module_obj && main_module_obj->isHttpService()) {
		cmd = "gix-http";
		tmp_cfg_path = PathUtils::combine(QDir::tempPath(), "gix_http_" + SysUtils::randomString(6) + ".config");

		QString program_id = CobolUtils::extractProgramId(main_module_obj->GetFileFullPath());
		if (program_id.isEmpty()) {
			QString msg = tr("Invalid project configuration");
			ide_task_manager->logMessage(GIX_CONSOLE_LOG, msg, QLogger::LogLevel::Error);
			UiUtils::ErrorDialog(msg);
			return false;
		}

		int http_port = SysUtils::getSubProperty(main_module_obj->PropertyGetValue("is_rest_ws").toString(), "port").toInt();
		if (!http_port)
			http_port = 9090;

		ServerConfig svr;
		svr.setAddress("127.0.0.1");
		svr.setPort(http_port);
		svr.setRuntimePath(compiler_cfg->homeDir);
		svr.setDebugEnabled(is_debugging_enabled);
		svr.setLog(PathUtils::combine(PathUtils::getDirectory(target_full_path),
			svr.getAddressString().replace(".", "_") + "__" + QString::number(svr.getPort()) + ".log"));
		svr.setLogLevel("debug");
		svr.setLogConsoleEchoEnabled(true);
		svr.setSearchPath(mm.translate(output_path));

		if (main_module_obj->isRestService()) {
			svc_rest = ServiceConfig::ofType("rest");
			svc_rest->setName(startup_item_name);
			svc_rest->setProgram(startup_item_name);
			svc_rest->setDescription(QString("Service (REST) %1 at %2").arg(startup_item_name).arg(svr.getServerId()));
			svc_rest->setEnabled(true);
			svc_rest->setUrl("/" + startup_item_name);

			QString itf_in_fld = SysUtils::getSubProperty(main_module_obj->PropertyGetValue("is_rest_ws").toString(), "interface_in_field").toString();
			if (itf_in_fld.isEmpty()) {
				ide_task_manager->logMessage(GIX_CONSOLE_LOG, "Invalid input field", QLogger::LogLevel::Error);
				return false;
			}
			svc_rest->setInterfaceInFieldName(itf_in_fld);

			QString itf_out_fld = SysUtils::getSubProperty(main_module_obj->PropertyGetValue("is_rest_ws").toString(), "interface_out_field").toString();
			if (itf_out_fld.isEmpty()) {
				ide_task_manager->logMessage(GIX_CONSOLE_LOG, "Invalid output field", QLogger::LogLevel::Error);
				return false;
			}
			svc_rest->setInterfaceOutFieldName(itf_out_fld);

			svc_rest->setLog(PathUtils::combine(build_dir, svc_rest->getName() + ".log"));
			svc_rest->setLogLevel("debug");
			svr.addService(startup_item_name, svc_rest);
		}

		if (main_module_obj->isSoapService()) {
			ide_task_manager->logMessage(GIX_CONSOLE_LOG, "Unsupported function", QLogger::LogLevel::Error);
			return false;
			//svc_soap = ServiceConfig::ofType("soap");
			//svc_soap->setName(startup_item_name);
			//svc_soap->setProgram(startup_item_name);
			//svc_soap->setDescription(QString("Service (SOAP) %1 at %2").arg(startup_item_name).arg(svr.getServerId()));
			//svc_soap->setEnabled(true);
			//svc_soap->setUrl("/" + startup_item_name);

			//svc_soap->setInterfaceIn("P:\\gix-ide\\test\\wstest\\WSINTF.cpy");
			//svc_soap->setInterfaceOut("P:\\gix-ide\\test\\wstest\\WSINTF.cpy");
			//svc_soap->setLog(PathUtils::combine(PathUtils::getDirectory(target), svc_soap->getName() + ".log"));
			//svc_soap->setLogLevel("debug");
			//svr.addService(startup_item_name, svc_soap);
		}

		if (!svr.write(tmp_cfg_path)) {
			QLogger::QLog_Error(GIX_CONSOLE_LOG, "Cannot write configuration file " + tmp_cfg_path);
			return false;
		}


		cobcrun_opts.append(tmp_cfg_path);
	}
	else {
		if (!uses_external_cmd) {
			if (build_type == "dll") {
				cmd = compiler_cfg->runnerPath;
				cobcrun_opts.append("-M");
				//cobcrun_opts.append(PathUtils::getDirectory(target));
				cobcrun_opts.append(target);
				cobcrun_opts.append(module_name);
			}
			else
				cmd = target_full_path;
		}
		else {
			cmd = prj->PropertyGetValue("dbg_cmd").toString();
		}
	}

	QStringList cmd_args = parseArguments(prj->PropertyGetValue("dbg_args").toString());
	cobcrun_opts.append(cmd_args);

	GixDebugger *gd = GixDebugger::get();
	gd->setVerbose(ide_task_manager->isDebugOutputEnabled());

	QMap<QString, QString> dbg_env;

	for (auto k : env.keys()) {
		QString v = env.value(k);
		dbg_env[k] = v;
	}
	gd->setEnvironment(dbg_env);
	gd->setProperty("symformat", compiler_cfg->isVsBased ? "pdb" : "dwarf");
	gd->setWorkingDirectory(working_dir);
	gd->setModuleDirectory(QFileInfo(target_full_path).path());
	gd->setProcess(cmd);
	gd->setCommandLine(cobcrun_opts.join(" "));
	gd->setDebuggedModuleType(build_type == "dll" ? DebuggedModuleType::Shared : DebuggedModuleType::Executable);
	gd->setDebuggingEnabled(is_debugging_enabled);
	gd->setUseExternalConsole(dbg_separate_console);

	debug_driver = new DebugDriver(this);
	debug_driver->setDebuggerInstance(gd);

	QObject::connect(debug_driver, &DebugDriver::DebuggerProcessFinished, this, [this](QString m, int l) { debuggedProcessFinished(l, m); }, Qt::ConnectionType::QueuedConnection);
	QObject::connect(debug_driver, &DebugDriver::DebuggerProcessStarted, this, [this](QString m) { debuggedProcessStarted(); }, Qt::ConnectionType::QueuedConnection);
	QObject::connect(debug_driver, &DebugDriver::DebuggerProcessError, this, [this](QString m, int l) { debuggedProcessError(l, m); }, Qt::ConnectionType::QueuedConnection);
	QObject::connect(debug_driver, &DebugDriver::DebuggerReady, debug_driver, [this](QString socket_id) {});
	QObject::connect(debug_driver, &QThread::finished, debug_driver, &QThread::deleteLater);

	QObject::connect(debug_driver, &DebugDriver::DebuggerStdOutAvailable, this, [this](QString m) { ide_task_manager->consoleWriteStdOut(m); }, Qt::ConnectionType::QueuedConnection);
	QObject::connect(debug_driver, &DebugDriver::DebuggerStdErrAvailable, this, [this](QString m) { ide_task_manager->consoleWriteStdErr(m); }, Qt::ConnectionType::QueuedConnection);

	if (is_debugging_enabled) {
		loadDebugManagerState();

		QObject::connect(debug_driver, &DebugDriver::DebuggerBreak, this, [this](QString m, QString s, int l) { debug_break(m, s, l); }, Qt::ConnectionType::QueuedConnection);
		QObject::connect(debug_driver, &DebugDriver::DebuggerModuleChanged, this, [this](QString m, int l) { debug_module_changed(m, l); }, Qt::ConnectionType::QueuedConnection);

		QObject::connect(debug_driver, &DebugDriver::DebuggerModuleExit, this, [this](QString m, int l) { debug_program_exit(m, l); }, Qt::ConnectionType::QueuedConnection);
	}

	ide_task_manager->consoleClear();

	debug_driver->start();

	return true;
}


void DebugManager::readStdErr()
{
	QProcess *p = (QProcess *)sender();
	p->setReadChannel(QProcess::ProcessChannel::StandardError);
	QString s = p->readAll();
	ide_task_manager->logMessage(GIX_CONSOLE_LOG, s, QLogger::LogLevel::Error);
}

void DebugManager::readStdOut()
{
	QProcess *p = (QProcess *)sender();
	p->setReadChannel(QProcess::ProcessChannel::StandardOutput);
	QString s = p->readAll();
	ide_task_manager->logMessage(GIX_CONSOLE_LOG, s, QLogger::LogLevel::Info);
}

void DebugManager::debuggedProcessFinished(int rc, QString s)
{
	QString hexCode;
	hexCode.setNum((unsigned int)rc, 16);
	hexCode = "0x" + hexCode.rightJustified(8, '0');
	QString msg = QString(tr("Process finished with exit code %1 (%2)")).arg(rc).arg(hexCode);
	ide_task_manager->logMessage(GIX_CONSOLE_LOG, msg, QLogger::LogLevel::Info);
	if (rc) {
		UiUtils::ErrorDialog(msg);
	}

	emit debugStopped();

}

void DebugManager::debuggedProcessError(int errcode, QString errmsg)
{

	QString msg = QString(tr("The process stopped with an error (%1): %2")).arg(errcode).arg(errmsg);
	ide_task_manager->logMessage(GIX_CONSOLE_LOG, msg, QLogger::LogLevel::Error);
	//ide_task_manager->logMessage(GIX_CONSOLE_LOG, launched_process->errorString(), QLogger::LogLevel::Info);
	if (!is_user_initiated_stop) {
		UiUtils::ErrorDialog(msg);
	}

	//   __TRACE("DebugManager: emitting debugStopped");
	emit debugError();
}

void DebugManager::debuggedProcessStarted()
{
	//	__TRACE("DebugManager: emitting debugStarted");
	emit debugStarted();
}

void DebugManager::debug_module_changed(QString module, int ln)
{
	ide_task_manager->logMessage(GIX_CONSOLE_LOG, "DBG: MODULE CHANGED @" + module + ":" + QString::number(ln), QLogger::LogLevel::Debug);
}

void DebugManager::debug_program_exit(QString m, int l)
{
	//	__TRACE("DebugManager: emitting debugProgramExit");
	emit debugProgramExit();
}

void DebugManager::debug_break(QString module_name, QString src_file, int ln)
{
	ide_task_manager->logMessage(GIX_CONSOLE_LOG, "DBG: BREAK @" + src_file + ":" + QString::number(ln), QLogger::LogLevel::Debug);

	bool bkp_located = false;

	CobolModuleMetadata *cmm = GixGlobals::getMetadataManager()->getModuleMetadata(module_name);
	if (cmm) {
		if (!cmm->isPreprocessedESQL()) {	// Type 1
			cur_src_file = src_file;
			cur_line = ln;
			bkp_located = true;
		}
		else {
			bkp_located = translateBreakpointReverse(cmm, src_file, ln, cur_src_file, &cur_line);
		}

		if (bkp_located) {
			ide_task_manager->logMessage(GIX_CONSOLE_LOG, "Located line " + QString::number(ln) + " for file " + src_file, QLogger::LogLevel::Trace);
			ide_task_manager->setStatus(IdeStatus::DebuggingOnBreak);

			emit ide_task_manager->IdeDebuggerBreak();
			emit ide_task_manager->IdeEditorChangedPosition(cur_src_file, cur_line);
		}
		else {
			ide_task_manager->logMessage(GIX_CONSOLE_LOG, "Cannot locate line " + QString::number(ln) + " for file " + src_file, QLogger::LogLevel::Error);

		}
	}
	else {
		ide_task_manager->logMessage(GIX_CONSOLE_LOG, "Cannot locate module for file " + src_file, QLogger::LogLevel::Error);
	}
}

void DebugManager::step()
{
	QString module;
	int ln;

	ide_task_manager->setStatus(IdeStatus::Debugging);

	driverWrite(DebugDriver::CMD_STEP);
}

void DebugManager::stop()
{
	if (debug_driver)
		debug_driver->stop();

	driverWrite(DebugDriver::CMD_CONTINUE);

	//debug_driver->quit();
}

void DebugManager::continue_running()
{
	QString module;
	int ln;

	ide_task_manager->setStatus(IdeStatus::Debugging);

	driverWrite(DebugDriver::CMD_CONTINUE);
	QString resp = debug_driver->getLastResponse();
}

QString DebugManager::getCurrentSourceFile()
{
	return QDir::cleanPath(cur_src_file);
}

QString DebugManager::getCurrentCobolModuleName()
{
	return cur_module;
}

int DebugManager::getCurrentLine()
{
	return cur_line;
}

bool DebugManager::parsePosition(QString pos, QString &module, int *line)
{
	QRegularExpression rx("^([A-Za-z0-9\\-_]+):([0-9]+)$");
	QRegularExpressionMatch m = rx.match(pos);

	if (m.hasMatch()) {
		module = m.captured(1);
		*line = m.captured(2).toInt();
		return true;
	}
	return false;
}

void DebugManager::loadDebugManagerState()
{
	ProjectCollection *ppj = Ide::TaskManager()->getCurrentProjectCollection();
	if (ppj == nullptr)
		return;

	watched_vars.append(Ide::TaskManager()->getWatchedVars());
}

void DebugManager::saveDebugManagerState()
{
	ProjectCollection *ppj = Ide::TaskManager()->getCurrentProjectCollection();
	if (ppj == nullptr)
		return;

	Ide::TaskManager()->setWatchedVars(watched_vars);
}

QString DebugManager::driverWrite(const QString &msg)
{
	//driver_client.write(msg.toLocal8Bit().constData());
	debug_driver->write(msg);
	return "OK";
}

QStringList DebugManager::parseArguments(QString args)
{
	if (args.isEmpty())
		return QStringList();

	bool inside = (args.at(0) == "\""); //true if the first character is "
	QStringList tmpList = args.split(QRegExp("\""), QString::SkipEmptyParts); // Split by " and make sure you don't have an empty string at the beginning
	QStringList arglist;
	foreach(QString s, tmpList)
	{
		if (inside) { // If 's' is inside quotes ...
			arglist.append(s); // ... get the whole string
		}
		else { // If 's' is outside quotes ...
			arglist.append(s.split(" ", QString::SkipEmptyParts)); // ... get the splitted string
		}
		inside = !inside;
	}
	return arglist;
}

QString DebugManager::getPrintableVarContent(QString n)
{
	driverWrite(DebugDriver::CMD_GET_VAR + n);
	QString value = debug_driver->getLastResponse();
	if (value.startsWith("OK:"))
		return value.mid(3);
	else
		return value;
}

QMap<QString, QString> DebugManager::getPrintableVarListContent(QStringList vlist)
{
	QMap<QString, QString> res;
	if (!vlist.size())
		return res;

	QList<VariableData *> var_req;
	for (QString var_name : vlist) {
		VariableData *vd = new VariableData();
		vd->var_name = var_name;
		var_req.push_back(vd);
	}

	if (debug_driver->debuggerInstance()->getVariables(var_req)) {
		for (VariableData *vd : var_req) {
			// TODO: format data
			res[vd->var_name] = ((char *)vd->data) ? (char *)vd->data : "N/A";
		}
	}

	return res;
}

int DebugManager::getWatchedVarCount()
{
	return watched_vars.size();
}

QString DebugManager::getWatchedVarName(int i)
{
	return watched_vars.at(i);
}

void DebugManager::addWatchedVar(QString s)
{
	if (!watched_vars.contains(s))
		watched_vars.append(s);
}

void DebugManager::removeWatchedVar(QString s)
{
	if (watched_vars.contains(s))
		watched_vars.removeOne(s);
}

QStringList DebugManager::getWatchedVarList()
{
	return watched_vars;
}

QStringList DebugManager::getTranslatedBreakpoints()
{
	int rid, rln;

	bool dbg_stop_at_first_line = debugged_prj->PropertyGetValue("dbg_stop_at_first_line").toBool();
	QStringList breakpoints;
	//if (dbg_stop_at_first_line)
	//	breakpoints.append("ANY:-1");

	breakpoints.append(Ide::TaskManager()->getBreakpoints());

	QString module_name = debug_driver->debuggerInstance()->getCurrentCobolModuleName();
	CobolModuleMetadata *cmm = GixGlobals::getMetadataManager()->getModuleMetadata(module_name);
	if (!cmm) {
		ide_task_manager->logMessage(GIX_CONSOLE_LOG, QString(tr("Cannot locate symbol data for module %1, no breakpoints avalable")).arg(module_name), QLogger::LogLevel::Warning);
		return QStringList();
	}

	bool is_esql = cmm->isPreprocessedESQL();

	if (!is_esql)
		return breakpoints;
	else
		return translateBreakpoints(cmm, breakpoints);
}

void DebugManager::setUserInititatedStop(bool b)
{
	is_user_initiated_stop = b;
}

QStringList DebugManager::translateBreakpoints(CobolModuleMetadata *cmm, const QStringList &orig_bkps)
{
	QStringList tx_bkps;
	for (QString bkp : orig_bkps) {
		QString orig_src_file = bkp.mid(bkp.indexOf("@") + 1);
		int orig_ln = bkp.mid(0, bkp.indexOf("@")).toInt();

		QString running_file;
		int running_file_id = 0;
		int running_line = 0;
		if (!cmm->originalToRunning(cmm->originalFileId(), orig_ln, &running_file_id, &running_line))
			continue;

		if (!cmm->getFileById(running_file_id, running_file))
			continue;

		QString tx_bkp = QString::number(running_line) + "@" + running_file;
		tx_bkps.append(tx_bkp);
	}
	return tx_bkps;
}

bool DebugManager::translateBreakpointReverse(CobolModuleMetadata *cmm, const QString &running_file, int running_ln, QString &orig_file, int *orig_ln)
{
	QString r_orig_file;
	int r_orig_ln = 0;
	int r_orig_file_id = 0;

	if (!cmm->runningToOriginal(cmm->runningFileId(), running_ln, &r_orig_file_id, &r_orig_ln))
		return false;

	if (!cmm->getFileById(r_orig_file_id, r_orig_file))
		return false;

	orig_file = r_orig_file;
	*orig_ln = r_orig_ln;

	return true;
}