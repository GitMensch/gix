#pragma once

#include <string>
#include <vector>
#include <map>
#include <libpq-fe.h>

#include "ILogger.h"
#include "ICursor.h"
#include "IDbInterface.h"
#include "IDbManagerInterface.h"
#include "IConnectionString.h"
#include "ISchemaManager.h"

using namespace std;

class DbInterfacePGSQL : public IDbInterface, public IDbManagerInterface
{
public:
	DbInterfacePGSQL();
	~DbInterfacePGSQL();

	virtual int init(ILogger *) override;
	virtual int connect(IConnectionString *, int, string) override;
	virtual int reset() override;
	virtual int terminate_connection() override;
	virtual int begin_transaction() override;
	virtual int end_transaction(string) override;
	virtual int exec(string) override;
	virtual int exec_params(string, int, int *, vector<string>&, int *, int *, int) override;
	virtual int close_cursor(ICursor *) override;
	virtual int cursor_declare(ICursor *, bool, int) override;
	virtual int cursor_declare_with_params(ICursor *, char **, bool, int) override;
	virtual int cursor_open(ICursor* cursor);
	virtual int fetch_one(ICursor *, int) override;
	virtual bool get_resultset_value(ICursor* c, int row, int col, char* bfr, int bfrlen);
	virtual int move_to_first_record() override;
	virtual int supports_num_rows() override;
	virtual int get_num_rows() override;
	virtual int get_num_fields() override;
	virtual char *get_error_message() override;
	virtual int get_error_code() override;
	virtual void set_owner(IConnection *) override;
	virtual IConnection* get_owner() override;

	virtual bool getSchemas(vector<SchemaInfo*>& res) override;
	virtual bool getTables(string table, vector<TableInfo*>& res) override;
	virtual bool getColumns(string schema, string table, vector<ColumnInfo*>& columns) override;
	virtual bool getIndexes(string schema, string tabl, vector<IndexInfo*>& idxs) override;

private:
	PGconn *connaddr;
	PGresult *resaddr;

	int last_rc;
	string last_error;

	map<string, ICursor*> _declared_cursors;

	int _pgsql_exec_params(ICursor*, const string, int, int*, vector<string>&, int*, int*, int);
	int _pgsql_exec(ICursor*, const string);

};
