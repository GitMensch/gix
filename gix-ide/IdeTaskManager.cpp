#include "IdeTaskManager.h"
#include "Ide.h"
#include "BuildDriver.h"
#include "ProjectCollection.h"
#include "OutputWindow.h"
#include "ProjectCollectionWindow.h"
#include "MdiChild.h"
#include "PathUtils.h"
#include "UiUtils.h"
#include "SysUtils.h"
#include "ConsoleWindow.h"
#include "MetadataLoader.h"
#include "DataEntry.h"
#include "GixGlobals.h"
#include "linq/linq.hpp"

#include <QComboBox>
#include <QFile>
#include <QList>
#include <QSettings>
#include <QMdiSubWindow>
#include <QStatusBar>
#include <QProgressBar>
#include <QLabel>
#include <QApplication>
#include <QMetaEnum>
#include <QMdiArea>
#include "QLogger.h"

IdeTaskManager::IdeTaskManager()
{
	main_window = nullptr;
	output_window = nullptr;
	prjcoll_window = nullptr;
	console_window = nullptr;
	watch_window = nullptr;
	navigation_window = nullptr;
	debug_manager = nullptr;
	current_project_collection = nullptr;
	ide_status = IdeStatus::Started;
	background_tasks_enabled = false;

	current_bookmark = 0;

    qRegisterMetaType<QLogger::LogLevel>();
	qRegisterMetaType<QList<ProjectFile *>>("QList<ProjectFile *>");

	//log_manager = QLogger::QLoggerManager::getInstance();
	//log_manager->addDestination("virtual://", GIX_CONSOLE_LOG, QLogger::LogLevel::Debug);
 //   connect(log_manager, &QLogger::QLoggerManager::logMessage, this, &IdeTaskManager::logMessage);


	connect(this, &IdeTaskManager::fileAddedToProject, this, [this] (ProjectFile *pf) { prjcoll_window->refreshContent(); });

	/*
	connect(this, &IdeTaskManager::fileSaved, this, [this] (ProjectFile * pf){
		logMessage(GIX_CONSOLE_LOG, QString("File saved"), QLogger::LogLevel::Debug);
		if (pf) {
			// Here we should try to regenerate the .sym file and reload it

			//logMessage(GIX_CONSOLE_LOG, QString("Updating metadata for %1").arg(pf->GetFileFullPath()), QLogger::LogLevel::Debug);
			//MetadataLoader* mu = new MetadataLoader();
			//mu->setScanTarget(pf);
			//mu->setConfiguration(getCurrentConfiguration());
			//mu->setPlatform(getCurrentPlatform());
			//connect(mu, &MetadataLoader::finishedUpdating, this, [this](bool b) { emit this->finishedUpdatingMetadata(b); });
			//logMessage(GIX_CONSOLE_LOG, QString("Started updating metadata for %1").arg(pf->GetFileFullPath()), QLogger::LogLevel::Debug);
			//mu->start();
		}
		
	});*/
}

DebugManager *IdeTaskManager::getDebugManager()
{
	return debug_manager;
}

IdeTaskManager::~IdeTaskManager()
{
	if (debug_manager != nullptr)
		debug_manager->deleteLater();
}

void IdeTaskManager::setCurrentProjectCollection(ProjectCollection *ppj)
{
	current_project_collection_data.clear();
	current_project_collection = ppj;
	if (ppj != nullptr)
		ide_status = IdeStatus::Editing;
}

ProjectCollection * IdeTaskManager::getCurrentProjectCollection()
{
	return current_project_collection;
}

Project* IdeTaskManager::getCurrentProject()
{
	ProjectCollection* ppj = getCurrentProjectCollection();
	if (!ppj) 
		return nullptr;

	ProjectItem* pi = prjcoll_window->getCurrentSelection();
	if (pi && pi->GetItemType() == ProjectItemType::TProject)
		return (Project*)pi;

	MdiChild* c = main_window->activeMdiChild();
	if (!c)
		return nullptr;

	ProjectFile* pf = ppj->locateProjectFileByPath(c->currentFile(), true);
	if (!pf)
		return nullptr;

	Project* prj = pf->getParentProject();
	return prj;
}

void IdeTaskManager::init(MainWindow *mw, OutputWindow *ow, ProjectCollectionWindow *pcw, ConsoleWindow* cw, WatchWindow *ww, NavigationWindow *nw)
{
	main_window = mw;
	output_window = ow;
	prjcoll_window = pcw;
	console_window = cw;
	watch_window = ww;
	navigation_window = nw;

	connect(Ide::TaskManager(), &IdeTaskManager::projectCollectionLoaded, this, [this] {

		ProjectCollection *prj_coll = Ide::TaskManager()->getCurrentProjectCollection();
		auto prjs = cpplinq::from(*(prj_coll->GetChildren())).where([](ProjectItem *a) { return a->GetItemType() == ProjectItemType::TProject;  }).to_vector();
		for (ProjectItem *ppi : prjs) {
			Project *prj = (Project *)ppi;

			for (ProjectFile *pf : prj->getAllCompilableFiles()) {
				emit GixGlobals::getMetadataManager()->scanCobolModule(pf);
			}
		}

	});
}

void IdeTaskManager::buildAll(QString configuration, QString platform)
{
	if (current_project_collection == nullptr || !current_project_collection->HasChildren())
		return;

	setStatus(IdeStatus::Building);

	int build_res = 0;
	QList<QPair<QString, QString>> tl;

	auto compiler_cfg = CompilerConfiguration::get(configuration, platform);

	QVariantMap props;
	props.insert("sys.objext", SysUtils::isWindows() && compiler_cfg->isVsBased ? ".obj" : ".o");
	props.insert("sys.dllext", SysUtils::isWindows() ? ".dll" : ".so");
	props.insert("sys.exeext", SysUtils::isWindows() ? ".exe" : "");
	props.insert("prj.build_dir", "${prj.basedir}/bin/${configuration}/${platform}");
	props.insert("prj.output_path", "${output_path}");
	props.insert("configuration", configuration);
	props.insert("platform", platform);

	BuildTarget *build_target = current_project_collection->getBuildTarget(props, nullptr);
	this->logMessage(GIX_CONSOLE_LOG, build_target->toString(), QLogger::LogLevel::Trace);

	BuildDriver builder;
	connect(&builder, &BuildDriver::log_output, output_window, &OutputWindow::print);
	connect(&builder, &BuildDriver::log_clear, output_window, &OutputWindow::clearAll);

	connect(this, &IdeTaskManager::stopBuildInvoked, &builder, &BuildDriver::stopBuild);

	builder.setBuildEnvironment(props);
		
	builder.execute(build_target, BuildOperation::Build);

	build_res += builder.getBuildResult().getStatus();

	if (builder.getBuildResult().getStatus() != 0) {

	}

	tl << builder.getBuiltTargetList();

	setStatus(IdeStatus::Editing);

	emit buildFinished(build_res);
}

void IdeTaskManager::buildClean(QString configuration, QString platform)
{
	if (current_project_collection == nullptr || !current_project_collection->HasChildren())
		return;

	CompilerConfiguration *compiler_cfg = CompilerConfiguration::get(configuration, platform);
	if (!compiler_cfg) {
		logMessage(GIX_CONSOLE_LOG, tr("Please check your compiler configuration"), QLogger::LogLevel::Error);
		return;
	}

	setStatus(IdeStatus::Building);

	QVariantMap props;
	props.insert("sys.objext", SysUtils::isWindows() && compiler_cfg->isVsBased ? ".obj" : ".o");
	props.insert("sys_dllext", SysUtils::isWindows() ? "dll" : "so");
	props.insert("sys.exeext", SysUtils::isWindows() ? ".exe" : "");
	props.insert("prj.build_dir", "${prj.basedir}/bin/${configuration}/${platform}");
	props.insert("configuration", configuration);
	props.insert("platform", platform);
	BuildTarget *build_target = current_project_collection->getBuildTarget(props, nullptr);
	BuildDriver builder;

	connect(&builder, &BuildDriver::log_output, output_window, &OutputWindow::print);
	connect(&builder, &BuildDriver::log_clear, output_window, &OutputWindow::clearAll);

	builder.setBuildEnvironment(props);

	builder.execute(build_target, BuildOperation::Clean);

	setStatus(IdeStatus::Editing);
}

void IdeTaskManager::buildStop()
{
	emit stopBuildInvoked();
}

void IdeTaskManager::debugProject(Project *prj, QString configuration, QString platform)
{
	auto compiler_cfg = CompilerConfiguration::get(configuration, platform);

	QMap<QString, QVariant> build_env;
	build_env.insert("configuration", configuration);
	build_env.insert("platform", platform);
	build_env.insert("sys.objext", SysUtils::isWindows() && compiler_cfg->isVsBased ? ".obj" : ".o");
	build_env.insert("sys.dllext", SysUtils::isWindows() ? ".dll" : ".so");
	build_env.insert("sys.exeext", SysUtils::isWindows() ? ".exe" : "");
	build_env.insert("prj.build_dir", "${prj.basedir}/bin/${configuration}/${platform}");
	build_env.insert("module_name", PathUtils::toModuleName(prj->GetFilename()));

	current_debug_target = prj->getBuildTarget(build_env, nullptr);
	if (!current_debug_target->isUpToDate()) {

	}

	setStatus(IdeStatus::Starting);

	debug_manager = new DebugManager(this);
	connect(debug_manager, &DebugManager::debugStopped, this, [this] { debugStopped();  } , static_cast<Qt::ConnectionType>(Qt::ConnectionType::AutoConnection | Qt::ConnectionType::UniqueConnection));
	connect(debug_manager, &DebugManager::debugError, this, [this] { debugError();  } , static_cast<Qt::ConnectionType>(Qt::ConnectionType::AutoConnection | Qt::ConnectionType::UniqueConnection));

	bool rc = debug_manager->start(prj, configuration, platform);
	if (rc) {
		main_window->setAllMdiChildrenReadOnly(true);
		setStatus(IdeStatus::Debugging);
	}
	else
		debugError();
}

void IdeTaskManager::runProject(Project *prj, QString configuration, QString platform, bool run_detached)
{
	if (debug_manager != nullptr) {
		debug_manager->deleteLater();
	}

	setStatus(IdeStatus::Starting);

	debug_manager = new DebugManager(this);
	connect(debug_manager, &DebugManager::debugStopped, this, &IdeTaskManager::debugStopped, static_cast<Qt::ConnectionType>(Qt::ConnectionType::AutoConnection | Qt::ConnectionType::UniqueConnection));
	connect(debug_manager, &DebugManager::debugError, this, &IdeTaskManager::debugError, static_cast<Qt::ConnectionType>(Qt::ConnectionType::AutoConnection | Qt::ConnectionType::UniqueConnection));
	connect(debug_manager, &DebugManager::debugStarted, this, &IdeTaskManager::debugStarted, static_cast<Qt::ConnectionType>(Qt::ConnectionType::AutoConnection | Qt::ConnectionType::UniqueConnection));
	debug_manager->setDebuggingEnabled(false);
	bool rc = debug_manager->start(prj, configuration, platform);
	if (rc) {
		debug_manager = nullptr;
	}
}

void IdeTaskManager::debugStep()
{
	if (debug_manager == nullptr || ide_status != IdeStatus::DebuggingOnBreak)
		return;

	debug_manager->step();
}

void IdeTaskManager::debugStop()
{
	if (debug_manager == nullptr || (ide_status != IdeStatus::Debugging && 
									ide_status != IdeStatus::DebuggingOnBreak &&
									ide_status != IdeStatus::Running)) {
		debug_manager = nullptr;
		return;
	}
	debug_manager->setUserInititatedStop(true);
	debug_manager->stop();

}

void IdeTaskManager::debugContinue()
{
	if (debug_manager == nullptr || ide_status != IdeStatus::DebuggingOnBreak)
		return;

	debug_manager->continue_running();
}

void IdeTaskManager::debugError()
{
	if (debug_manager) {
		delete debug_manager;
		debug_manager = nullptr;
	}

	main_window->setAllMdiChildrenReadOnly(false);
	main_window->removeAllDebugMarkers();

	if (current_debug_target) {
		delete current_debug_target;
		current_debug_target = nullptr;
	}

	setStatus(IdeStatus::Editing, true);
}

void IdeTaskManager::debugStopped()
{
	if (debug_manager) {
		delete debug_manager;
		debug_manager = nullptr;
	}

	main_window->setAllMdiChildrenReadOnly(false);
	main_window->removeAllDebugMarkers();

	if (current_debug_target) {
		delete current_debug_target;
		current_debug_target = nullptr;
	}

	setStatus(IdeStatus::Editing, true);
}

bool IdeTaskManager::loadProjectCollection(QString fileName)
{
	if (getCurrentProjectCollection() != nullptr) {
		if (!closeCurrentProjectCollection())
			return false;
	}

	ide_element_info_map.clear();

	ProjectCollection *ppj = new ProjectCollection();
	if (ppj->load(nullptr, fileName)) {
		main_window->prependToRecentProjects(fileName);
		prjcoll_window->setContent(ppj);
		setCurrentProjectCollection(ppj);
		loadProjectCollectionState(ppj);
		setStatus(IdeStatus::Editing, true);

		emit projectCollectionLoaded();

		return true;
	}

	return false;
}

void IdeTaskManager::loadProjectCollectionState(ProjectCollection *ppj)
{
	resetCurrentProjectCollectionState();

	QString ppj_file = ppj->GetFileFullPath();
	QString state_file = PathUtils::changeExtension(ppj_file, ".gixstate");
	QFile sf(state_file);
	if (!sf.exists())
		return;

	QSettings settings(state_file, QSettings::IniFormat, this);
	QString configuration = settings.value("ide/configuration", "").toString();
	QString platform = settings.value("ide/platform", "").toString();

	if (configuration != "" && main_window->cbConfiguration->findData(configuration) >= 0)
		main_window->cbConfiguration->setCurrentIndex(main_window->cbConfiguration->findData(configuration));

	if (platform != "" && main_window->cbPlatform->findData(platform) >= 0)
		main_window->cbPlatform->setCurrentIndex(main_window->cbPlatform->findData(platform));
	else {
		main_window->cbPlatform->setCurrentIndex(0);
	}
	
	QStringList watched_vars;
	int nvars = settings.value("debug/watch_count", 0).toInt();
	for (int i = 0; i < nvars; i++) {
		QString s = settings.value("debug/watched_var_" + QString::number(i + 1), "").toString();
		if (s != "")
			watched_vars.append(s);
	}
	setWatchedVars(watched_vars);

	QStringList breakpoints;
	int nbkps = settings.value("debug/breakpoint_count", 0).toInt();
	for (int i = 0; i < nbkps; i++) {
		QString s = settings.value("debug/breakpoint_" + QString::number(i + 1), "").toString();
		if (s != "" && !breakpoints.contains(s))
			breakpoints.append(s);
	}
	setBreakpoints(breakpoints);

	QStringList bookmarks;
	int nbkmks = settings.value("bookmarks/bookmark_count", 0).toInt();
	for (int i = 0; i < nbkmks; i++) {
		QString s = settings.value("bookmarks/bookmark_" + QString::number(i + 1), "").toString();
		if (s != "" && !bookmarks.contains(s))
			bookmarks.append(s);
	}
	setBookmarks(bookmarks);

	int nfiles = settings.value("edit_files/count", 0).toInt();
	for (int i = 1; i <= nfiles; i++) {
		QString filename = settings.value("edit_files/file" + QString::number(i), "").toString();
		if (QFile(filename).exists()) {
			main_window->openFile(filename);
		}
	}
}


void IdeTaskManager::resetCurrentProjectCollectionState()
{
	setWatchedVars(QStringList());
	setBreakpoints(QStringList());
	setBookmarks(QStringList());

	clearModuleMetadata();

	ide_element_info_map.clear();
}

void IdeTaskManager::clearModuleMetadata()
{
	if (module_metadata_filemap.size()) {
		QMap<QString, CobolModuleMetadata*>::iterator i;
		for (i = module_metadata_filemap.begin(); i != module_metadata_filemap.end(); ++i) {
			CobolModuleMetadata* cfm = i.value();
			if (cfm)
				delete cfm;
		}
		module_metadata_filemap.clear();
		module_metadata_map.clear();
	}
}


bool IdeTaskManager::closeCurrentProjectCollection()
{
	if (getCurrentProjectCollection() != nullptr) {
		ProjectCollection * ppj = getCurrentProjectCollection();
		if (!ppj->save(nullptr, ppj->GetFileFullPath())) {
			QString msg = QString(tr("Cannot close/save project collection %1 or one of its dependent projects")).arg(ppj->GetDisplayName());
			UiUtils::ErrorDialog(msg);
			return false;
		}

		saveCurrentProjectCollectionState();

		main_window->mdiArea->closeAllSubWindows();

		resetCurrentProjectCollectionState();

		delete current_project_collection;
		current_project_collection = nullptr;

		emit projectCollectionClosed();
	}

	return true;
}

IdeStatus IdeTaskManager::getStatus()
{
	return ide_status;
}

void IdeTaskManager::setStatus(IdeStatus s, bool force_signal)
{
	if (s != ide_status || force_signal) {
		ide_status = s;
		emit IdeStatusChanged(ide_status);
		logMessage(GIX_CONSOLE_LOG, QString("Ide status set to %1").arg(s), QLogger::LogLevel::Debug); //SysUtils::enum_val_to_str<IdeStatus>(s)

		switch (s) {
			case IdeStatus::Building:
			case IdeStatus::Debugging:
			case IdeStatus::DebuggingOnBreak:
			case IdeStatus::Running:
			case IdeStatus::Started:
			case IdeStatus::Starting:
				background_tasks_enabled = false;
				break;

			case IdeStatus::Editing:
				background_tasks_enabled = true;
				break;
		}
	}
}

void IdeTaskManager::addBreakpoint(QString src_file, int line)
{
	if (!current_project_collection_data.contains("breakpoints")) {
		current_project_collection_data["breakpoints"] = QStringList();
	}
	QStringList bkps = current_project_collection_data["breakpoints"].toStringList();
	bkps.append(QString::number(line) + "@" + src_file);
	current_project_collection_data["breakpoints"] = bkps;

	saveCurrentProjectCollectionState();
}

void IdeTaskManager::removeBreakpoint(QString src_file, int line)
{
	if (!current_project_collection_data.contains("breakpoints")) {
		current_project_collection_data["breakpoints"] = QStringList();
	}

	QStringList bkps = current_project_collection_data["breakpoints"].toStringList();
	QString bkpdef = QString::number(line) + "@" + src_file;
	if (bkps.contains(bkpdef))
		bkps.removeOne(bkpdef);

	current_project_collection_data["breakpoints"] = bkps;

	saveCurrentProjectCollectionState();
}

QStringList IdeTaskManager::getBreakpoints()
{
	if (!current_project_collection_data.contains("breakpoints")) {
		return QStringList();
	}
	else {
		return current_project_collection_data["breakpoints"].toStringList();
	}
}

void IdeTaskManager::setBreakpoints(QStringList l)
{
	current_project_collection_data["breakpoints"] = l;
}

bool IdeTaskManager::existsBreakpoint(QString src_file, int line)
{
	QString bkpdef = QString::number(line) + "@" + src_file;
	return (getBreakpoints().contains(bkpdef));
}

void IdeTaskManager::setWatchedVars(QStringList l)
{
	current_project_collection_data["watched_vars"] = l;
}

QStringList IdeTaskManager::getWatchedVars()
{
	if (!current_project_collection_data.contains("watched_vars")) {
		return QStringList();
	}
	else {
		return current_project_collection_data["watched_vars"].toStringList();
	}
}

void IdeTaskManager::refreshWatchWindow()
{
	watch_window->refreshContent();
}

void IdeTaskManager::addBookmark(QString module, int line)
{
	if (!current_project_collection_data.contains("bookmarks")) {
		current_project_collection_data["bookmarks"] = QStringList();
	}
	QStringList bkmrks = current_project_collection_data["bookmarks"].toStringList();
	bkmrks.append(module + ":" + QString::number(line));
	current_project_collection_data["bookmarks"] = bkmrks;

	saveCurrentProjectCollectionState();
}

void IdeTaskManager::removeBookmark(QString module, int line)
{
	if (!current_project_collection_data.contains("bookmarks")) {
		current_project_collection_data["bookmarks"] = QStringList();
	}

	QStringList bkmrks = current_project_collection_data["bookmarks"].toStringList();
	QString bkmrkdef = module + ":" + QString::number(line);
	if (bkmrks.contains(bkmrkdef))
		bkmrks.removeOne(bkmrkdef);

	current_project_collection_data["bookmarks"] = bkmrks;

	saveCurrentProjectCollectionState();
}

void IdeTaskManager::setBookmarks(QStringList l)
{
	current_project_collection_data["bookmarks"] = l;
}

void IdeTaskManager::clearBookmarks(QString module)
{
	if (!current_project_collection_data.contains("bookmarks"))
		return;

	QStringList newlist;

	for (QString bmk : current_project_collection_data["bookmarks"].toStringList()) {
		if (!bmk.startsWith(module + ":"))
			newlist.append(bmk);
	}

	current_project_collection_data["bookmarks"] = newlist;
}

QStringList IdeTaskManager::getBookmarks()
{
	if (!current_project_collection_data.contains("bookmarks")) {
		return QStringList();
	}
	else {
		return current_project_collection_data["bookmarks"].toStringList();
	}
}

bool IdeTaskManager::existsBookmark(QString filename, int line)
{
	QString bkmrk = filename + ":" + QString::number(line);
	return (getBookmarks().contains(bkmrk));
}

bool IdeTaskManager::isDebugOutputEnabled()
{
	QSettings settings;
	return settings.value("Ide_DebugOutput", false).toBool();
}

QString IdeTaskManager::getNextBookmark()
{
	QStringList bms = getBookmarks();
	if (!bms.size())
		return QString();

	current_bookmark++;
	if (current_bookmark >= bms.size())
		current_bookmark = 0;

	return bms[current_bookmark];
}

QString IdeTaskManager::getPrevBookmark()
{
	QStringList bms = getBookmarks();
	if (!bms.size())
		return QString();

	current_bookmark--;
	if (current_bookmark < 0)
		current_bookmark = bms.size() - 1;

	return bms[current_bookmark];
}

QStringList IdeTaskManager::getBookmarks(QString filename)
{
	QStringList bms = getBookmarks();
	if (bms.isEmpty())
		return bms;

	QStringList res;
	filename = QDir::cleanPath(filename);
	for (QString bm : bms) {
		if (bm.startsWith(filename + ":"))
			res.append(bm);
	}
	return res;
}

void IdeTaskManager::statusShowMessage(QString msg)
{
	main_window->status_label->setText(msg);
}

void IdeTaskManager::statusSetRangeMin(int n)
{
	main_window->progress_bar->setMinimum(n);
}

void IdeTaskManager::statusSetRangeMax(int n)
{
	main_window->progress_bar->setMaximum(n);
}

void IdeTaskManager::statusSetRangeValue(int n)
{
	main_window->progress_bar->setValue(n);
}

void IdeTaskManager::statusSetRangeEnable(bool b)
{
	if (b)
		main_window->progress_bar->show();
	else
		main_window->progress_bar->hide();
}

QString IdeTaskManager::getCurrentConfiguration()
{
	return main_window->getCurrentConfiguration().toString();
}

QString IdeTaskManager::getCurrentPlatform()
{
	return main_window->getCurrentPlatform().toString();
}

void IdeTaskManager::consoleWriteStdOut(QString msg)
{
	console_window->appendOut(msg);
}

void IdeTaskManager::consoleWriteStdErr(QString msg)
{
	console_window->appendErr(msg);
}

void IdeTaskManager::consoleClear()
{
	console_window->clear();
}

//ListingFileParser *IdeTaskManager::getListingFileParser(ProjectFile *pf, QString module_name, QString lstPath)
//{
//	return listing_file_manager.get(pf, module_name, lstPath);
//}

void IdeTaskManager::flushLog()
{
	while (!log_backlog.isEmpty()) {
		LogBacklogEntry* lbl = log_backlog.dequeue();
		this->logMessage(lbl->module, lbl->msg, lbl->level);
		delete lbl;
	}
}


void IdeTaskManager::setIdeElementInfo(QString k, QVariant v)
{
	ide_element_info_map[k] = v;
}

QVariant IdeTaskManager::getIdeElementInfo(QString k)
{
	if (ide_element_info_map.contains(k))
		return ide_element_info_map[k];

	return QVariant();
}

void IdeTaskManager::gotoDefinition(CodeEditor *ce, QString def_path, int ln)
{
	if (!current_project_collection)
		return;

	MdiChild* c = dynamic_cast<MdiChild*>(ce);
	if (def_path.isEmpty())
		return;

	ProjectFile* pf = current_project_collection->locateProjectFileByPath(c->currentFile(), true);
	if (!pf)
		return;

	CobolModuleMetadata* cfm = GixGlobals::getMetadataManager()->getModuleMetadataBySource(pf->GetFileFullPath());
	if (!cfm)
		return;

	DataEntry* e = cfm->findDefinition(def_path);
	if (e)
		gotoDefinition(e); 
}

void IdeTaskManager::gotoDefinition(Paragraph* p)
{
	int lline = 0;
	if (p == nullptr || p->file == nullptr || p->line == 0)
		return;

	ProjectFile* pf = current_project_collection->locateProjectFileByPath(p->file, true);
	if (!pf)
		return;

	CobolModuleMetadata* cfm = GixGlobals::getMetadataManager()->getModuleMetadataBySource(pf->GetFileFullPath());
	if (!cfm)
		return;

	if (cfm->isPreprocessedESQL()) {
		ModuleDebugInfo* mdi = cfm->getDebugInfo();
		if (!mdi)
			return;

		//if (!mdi->hasSrcLineMapEntry(p->line))
		//	return;
		//else
		//	lline = mdi->getSourceLineMapEntry(p->line);
	}
	else
		lline = p->line;

	MdiChild* mdiChild = openFileNoSignals(p->file);

	if (!mdiChild)
		return;

	this->logMessage(GIX_CONSOLE_LOG, QString("Going to line %1").arg(lline), QLogger::LogLevel::Trace);

	mdiChild->highlightSymbol(lline, p->name);
}

void IdeTaskManager::gotoDefinition(DataEntry* e)
{
	QString file_to_open;
	int file_id;

	int lline = 0;
	if (e == nullptr || e->fileid == 0 || e->line == 0 || e->owner == nullptr)
		return;

	if (e->owner->isPreprocessedESQL()) {
		CobolModuleMetadata *cmm = e->owner;
		if (!cmm->runningToOriginal(e->fileid, e->line, &file_id, &lline))
			return;

		if (!cmm->getFileById(file_id, file_to_open))
			return;

	}
	else {
		file_to_open = e->filename;
		lline = e->line;
	}

	MdiChild* mdiChild = openFileNoSignals(file_to_open);

	if (!mdiChild)
		return;



	mdiChild->highlightSymbol(lline, e->name);
}

void IdeTaskManager::gotoFileLine(QString filename, int ln)
{
	if (!current_project_collection)
		return;

	if (filename.isEmpty() || !ln)
		return;

	ProjectFile *pf = current_project_collection->locateProjectFileByPath(filename, true);
	if (!pf)
		return;

	//this->main_window->blockMdiSignals(true);
	//bool b = this->main_window->openFile(e->file);
	//this->main_window->blockMdiSignals(false);
	//if (!b)
	//	return;

	//QMdiSubWindow* c = this->main_window->findMdiChild(e->file);
	//if (!c)
	//	return;

	//MdiChild* mdiChild = qobject_cast<MdiChild*>(c->widget());
	//this->main_window->blockMdiSignals(true);
	//mdiChild->activateWindow();
	//this->main_window->blockMdiSignals(false);
	//if (!mdiChild->isActiveWindow())
	//	return;

	MdiChild* mdiChild = openFileNoSignals(filename);
	if (!mdiChild)
		return;

	mdiChild->gotoLine(ln - 1);
}

bool IdeTaskManager::backgroundTasksEnabled()
{
	return background_tasks_enabled;
}

void IdeTaskManager::setBackgroundTasksEnabled(bool b)
{
	background_tasks_enabled = b;
}


void IdeTaskManager::debugStarted()
{
	setStatus(IdeStatus::Running);
}

void IdeTaskManager::startLoadingMetadata(ProjectItem *pi)
{
	if (!this->backgroundTasksEnabled())
		return;

	//MetadataLoader *u = new MetadataLoader();
	//connect(u, &MetadataLoader::finishedUpdating, this, [this] (bool changed) {
	//	this->logMessage(GIX_CONSOLE_LOG, "Finished updating project metadata", QLogger::LogLevel::Debug);
	//	emit finishedUpdatingMetadata(changed);

	//});
	//u->setScanTarget(pi);
	//u->setConfiguration(getCurrentConfiguration());
	//u->setPlatform(getCurrentPlatform());
	//u->start();
}

bool IdeTaskManager::isFileOpen(QString filename)
{
	QMdiSubWindow* c = this->main_window->findMdiChild(filename);
	return (c != nullptr);
}

bool IdeTaskManager::isFileModified(QString filename)
{
	QMdiSubWindow* c = this->main_window->findMdiChild(filename);
	return (c != nullptr) ? c->isWindowModified() : false;
}

MdiChild* IdeTaskManager::openFileNoSignals(QString filename)
{
	if (filename.isEmpty())
		return nullptr;

	this->main_window->blockMdiSignals(true);
	bool b = this->main_window->openFile(filename);
	this->main_window->blockMdiSignals(false);
	if (!b)
		return nullptr;

	QMdiSubWindow* c = this->main_window->findMdiChild(filename);
	if (!c)
		return nullptr;

	MdiChild* mdiChild = qobject_cast<MdiChild*>(c->widget());
	this->main_window->blockMdiSignals(true);
	mdiChild->activateWindow();
	this->main_window->blockMdiSignals(false);
	if (!mdiChild->isActiveWindow())
		return nullptr;

	return mdiChild;
}

void IdeTaskManager::logMessage(QString module, QString msg, QLogger::LogLevel log_level)
{
    if (this->receivers(SIGNAL(print(QString, QLogger::LogLevel)))) {
        emit this->print(msg, log_level);
    }
    else {
        LogBacklogEntry* lbl = new LogBacklogEntry();
        lbl->module = module;
        lbl->msg = msg;
        lbl->level = log_level;
        log_backlog.enqueue(lbl);
    }
}

void IdeTaskManager::saveCurrentProjectCollectionState()
{
	ProjectCollection *ppj = current_project_collection;
	if (!ppj)
		return;

	QString ppj_file = ppj->GetFileFullPath();
	QString state_file = PathUtils::changeExtension(ppj_file, ".gixstate");
	QFile sf(state_file);

	QSettings settings(state_file, QSettings::IniFormat, this);

	settings.setValue("ide/configuration", main_window->getCurrentConfiguration());
	settings.setValue("ide/platform", main_window->getCurrentPlatform());

	settings.beginGroup("edit_files");
	settings.remove("");
	settings.endGroup();

	QStringList open_files = main_window->getCurrentOpenFileList();

	settings.setValue("edit_files/count", open_files.size());
	int n = 1;
	for(QString open_file : open_files) {
		settings.setValue("edit_files/file" + QString::number(n++), open_file);
	}

	settings.beginGroup("debug");
	settings.remove("");
	settings.endGroup();

	QStringList watched_vars = getWatchedVars();
	settings.setValue("debug/watch_count", watched_vars.size());
	for (int i = 0; i < watched_vars.size(); i++) {
		settings.setValue("debug/watched_var_" + QString::number(i + 1), watched_vars.at(i));
	}

	QStringList breakpoints = getBreakpoints();
	settings.setValue("debug/breakpoint_count", breakpoints.size());
	for (int i = 0; i < breakpoints.size(); i++) {
		settings.setValue("debug/breakpoint_" + QString::number(i + 1), breakpoints.at(i));
	}

	QStringList bookmarks = getBookmarks();
	settings.setValue("bookmarks/bookmark_count", bookmarks.size());
	for (int i = 0; i < bookmarks.size(); i++) {
		settings.setValue("bookmarks/bookmark_" + QString::number(i + 1), bookmarks.at(i));
	}
}