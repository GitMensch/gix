#pragma once

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

#include <QObject>
#include <QStringList>
#include <QMap>
#include <QProcess>
#include <QThread>
//#include <QLocalServer>
//#include <QLocalSocket>
#include <QMutex>
#include <QWaitCondition>

#if defined(__MINGW32__)
typedef unsigned char byte;
#endif

#include <QProcessEnvironment>
#include <functional>

class GixDebugger;
class DebugManager;

class DebugDriverRunParameters {

public:
	//QStringList Breakpoints;
};

class DebugDriver : public QThread {
public:
	// Messages/Requests from debugged program to client debugger (VS, etc.)
	const static QString CMD_DEBUGGER_STARTING;
	const static QString CMD_GET_BREAKPOINTS;
	const static QString CMD_DBGRBREAK;
	const static QString CMD_DBGR_MOD_CHANGED;
	const static QString CMD_DBGR_MOD_EXIT;
	const static QString CMD_DBGR_PRG_EXIT;

	// Messages/Requests from client debugger (VS, etc.)  to debugged program
	const static QString CMD_GET_CURPOS;
	const static QString CMD_GET_VAR;
	const static QString CMD_GET_VARS;
	const static QString CMD_STEP;
	const static QString CMD_STEP_INTO;
	const static QString CMD_CONTINUE;
	const static QString CMD_ABORT;

	// Standard responses
	const static QString RESP_OK;
	const static QString RESP_OK_WITH_RESULT;

	const static QString RESP_KO;
	const static QString RESP_KO_WITH_RESULT;

	Q_OBJECT

public:
	DebugDriver(DebugManager *);
	~DebugDriver();

	void setDebuggerInstance(GixDebugger * gd);
	GixDebugger *debuggerInstance();

	bool stop();
	QString getLastResponse();
	void write(QString);
    bool isRunning();

	DebugDriverRunParameters RunParameters;

	static bool is_ok_response(QString);

//public slots:
    void run() override;

signals:
	void DebuggerReady(QString);
	void DebuggerBreak(QString, QString, int);
	//void DebuggerNewConnection(QString);
	//void DebuggerCommandSent(QString);
	//void DebuggerCommandReceived(QString);
	//void DebuggerLogMessage(QString);
	void DebuggerModuleChanged(QString, int);
	void DebuggerModuleExit(QString, int);
	void DebuggerProcessStarted(QString);
	void DebuggerProcessFinished(QString, int);
	void DebuggerProcessError(QString, int);
	void DebuggerStdOutAvailable(QString);
	void DebuggerStdErrAvailable(QString);

private:

	GixDebugger *debugger_instance = nullptr;

	QString current_module;

	QStringList response_queue;

	QStringList extract_args(QStringList);
	bool is_response(QString);

	bool processCommand(const QString &cmd_line);

	bool keep_waiting_host_cmds = false;
	bool is_running = false;

	QString last_response;

	DebugManager *debug_manager;

	QString cmd_line;
	QMutex cmd_mutex;
	QWaitCondition cmd_available;
};

