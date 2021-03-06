/*
This file is part of Gix-IDE, an IDE and platform for GnuCOBOL
Copyright (C) 2021 Marco Ridoni

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or (at
your option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
USA.
*/

#pragma once


#include <QString>
#include <QMap>
#include <QList>
#include <QStringList>
#include <QObject>

#ifdef _WIN32
#include <Windows.h>
#include <psapi.h>
#include <Dbghelp.h>
#include <tchar.h>
#endif


class GixDebugger;
class SourceLineInfo;
class SharedModuleInfo;
class CobolModuleInfo;

struct VariableResolverData;

class GixDebuggerInterfaceBlock
{

public:
	// Debugger events
	std::function<bool(GixDebugger *, QList<QPair<QString, int>> &)> getBreakpoints;
	std::function<bool(GixDebugger *, QString, QString, int)> debuggerBreak;
	std::function<bool(GixDebugger *, QString, int)> debuggerMessage;
	std::function<bool(GixDebugger *, int, QString)> debuggerError;
	std::function<bool(GixDebugger *, int, QString)> debuggerProcessExit;
	std::function<bool(GixDebugger *, QString)> debuggerProcessStarted;
	std::function<bool(GixDebugger *, QString)> debuggerReady;
	std::function<bool(GixDebugger *, QString)> debuggerStdOutAvailable;
	std::function<bool(GixDebugger *, QString)> debuggerStdErrAvailable;

	// Debugger host-provided utility functions
	std::function<bool(GixDebugger *gd, const QString &module_name, const QString &var_name, VariableResolverData *res)> resolveVariable;
};

class UserBreakpoint
{

public:
	QString key;
	QString source_file;
    int line = 0;

	void *address = 0x00000000;
	uint8_t orig_instr = 0x00;

	uint8_t automatic = false;

	SharedModuleInfo *owner = nullptr;

    bool isInstalled() { return orig_instr != 0x00; }
};

class SharedModuleInfo
{
public:
	~SharedModuleInfo();

	GixDebugger *owner = nullptr;

	QString name;
	QList<QString> source_files;
	QMap<QString, CobolModuleInfo *> cbl_modules;

	QString dll_path;
	void *dll_base = nullptr;
	void *dll_symbols = nullptr;
	void *dll_owner_process = nullptr;
	
	uint64_t base_of_code = 0;	// Used under Windows for DWARF objects
};

class CobolModuleInfo
{
public:
	SharedModuleInfo *owner = nullptr;
	QString name;
	void *entry_point = nullptr;;
	QMap<QString, VariableResolverData *> locals;

	UserBreakpoint *entry_breakpoint = nullptr;
	bool initialized = false;
};

class SourceLineInfo
{
public:
	SharedModuleInfo *module = nullptr;
	QString source_file;
	int line;

	void *addr = nullptr;
};

struct VariableResolverData
{
	QString module_name;
	QString var_name;
	QString var_path;

	int type = 0;
	int level = 0;
	int display_size = 0;
	int is_signed = 0;
	int decimals = 0;
	QString format;
	int storage_type = 0;
	QString storage;
	int occurs = 0;
	QString redefines;

	// The "root" variable
	QString base_var_name;

	QString local_sym_name;
	int local_addr = 0;
	
	int storage_len = 0;

	bool toDisplayFormat(const uint8_t *data, int data_len, uint8_t display_bfr, int display_len)
	{
		return false;
	}
};

enum class DebuggedModuleType
{
	Shared = 1,
	Executable = 2
};

struct VariableData
{
	QString var_name;
	//int storage_length = 0;
	VariableResolverData *resolver_data = nullptr;

	uint8_t *data = nullptr;
};

/*
	Strict typing for these functions is not actually needed, since we'll be calling them in the debugeee's context,
	but just in case...
*/
typedef const char *(*f_libcob_version_ptr);
typedef const char *(*f_cob_get_field_str_ptr)(const void *f, char *buffer, size_t size);
typedef int (*f_cob_put_field_str_ptr)(const void *dst, const char *str);
typedef void(*f_cob_runtime_error_ptr)(const char *fmt, ...);

struct LibCobInfo
{
	f_libcob_version_ptr f_libcob_version = nullptr;
	f_cob_get_field_str_ptr f_cob_get_field_str = nullptr;
	f_cob_put_field_str_ptr f_cob_put_field_str = nullptr;
	f_cob_runtime_error_ptr f_cob_runtime_error = nullptr;
};

class GixDebugger : public QObject
{
public:
	GixDebugger();
	virtual ~GixDebugger();

	void setDebuggingEnabled(bool b);
	void setProperty(const QString &k, const QString &v);
	void setInterfaceBlock(GixDebuggerInterfaceBlock *if_blk);
	void setEnvironment(QMap<QString, QString> vars);
	void setProcess(const QString &path);
	void setWorkingDirectory(const QString &wd);
	void setModuleDirectory(const QString &md);
	void setCommandLine(const QString &path);
	void setSourceFileTypes(const QList<QString> &_src_file_types);
	void setSymbolDirectories(const QList<QString> &_sym_dirs);
	void setBreakAtModuleStart(bool b);
	void setVerbose(bool b);
	void setUseExternalConsole(bool b);
	void setDebuggedModuleType(DebuggedModuleType b);

    bool usesExternalConsole();
    GixDebuggerInterfaceBlock *getInterfaceBlock();
	QString getWorkingDirectory();
	QString getModuleDirectory();

	DebuggedModuleType getDebuggedModuleType();

	void printMessage(QString msg);

	virtual int start() = 0;
	virtual int stop() = 0;
	virtual bool step() = 0;
	virtual bool continue_running() = 0;
	
	virtual bool getVariables(QList<VariableData *> var_list) = 0;
	virtual QString getCurrentCobolModuleName() = 0;

	static GixDebugger *get();

	bool existsBreakpoint(const QString &src_file, int ln);

	virtual void printLastError() = 0;

	virtual void removeHardwareBreakpoint(UserBreakpoint *bkp) = 0;
	virtual bool installHardwareBreakpoint(UserBreakpoint *bkp) = 0;

	virtual bool readProcessMemory(void *addr, void *bfr, int size) = 0;

    virtual void writeToProcess(QString s);

	QMap<void *, SourceLineInfo *>		source_lines_by_addr;
	QMap<QString, SourceLineInfo *> source_lines;
	QList<QString> src_file_types;

	QMap<QString, UserBreakpoint *> breakpoints;

protected:


	QMap<QString, QString> properties;

	bool break_at_module_start = true;
	bool verbose = false;
	DebuggedModuleType debuggee_type = DebuggedModuleType::Shared;
	GixDebuggerInterfaceBlock *if_blk;
	QMap<QString, QString> environment;
    int exit_code = 0;
	QString exepath;
	QString working_dir;
	QString module_dir;
	QString cmd_line_args;

	QList<SharedModuleInfo *> shared_modules;
	QList<QString> sym_dirs;

	void getAndResolveUserBreakpoints();
	UserBreakpoint *findBreakpointByAddress(void *addr);
	bool isCobolSourceFile(const QString &s);

	bool is_debugging_enabled = false;
	bool use_external_console = false;

	LibCobInfo *libcob_info = nullptr;

    bool isCblEntryPoint(void *addr, CobolModuleInfo **cmi);

private:

	virtual void *getSymbolAddress(const char *sym_name) = 0;
};
