/*-------------------------------------------------------------------------
 * Copyright (C) 2019, 4paradigm
 * dbms_sdk_test.cc
 *
 * Author: chenjing
 * Date: 2019/11/7
 *--------------------------------------------------------------------------
 **/

#include "sdk/dbms_sdk.h"
#include <unistd.h>
#include <map>
#include <string>
#include <utility>
#include "base/texttable.h"
#include "brpc/server.h"
#include "case/sql_case.h"
#include "dbms/dbms_server_impl.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "gtest/gtest.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include "tablet/tablet_server_impl.h"

DECLARE_string(dbms_endpoint);
DECLARE_string(endpoint);
DECLARE_int32(port);
DECLARE_bool(enable_keep_alive);

using namespace llvm;       // NOLINT
using namespace llvm::orc;  // NOLINT

namespace fesql {
namespace sdk {
using fesql::sqlcase::SQLCase;
std::vector<SQLCase> InitCases(std::string yaml_path);
void InitCases(std::string yaml_path, std::vector<SQLCase> &cases);  // NOLINT

void InitCases(std::string yaml_path, std::vector<SQLCase> &cases) {  // NOLINT
    if (!SQLCase::CreateSQLCasesFromYaml(fesql::sqlcase::FindFesqlDirPath(),
                                         yaml_path, cases)) {
        FAIL();
    }
}
std::vector<SQLCase> InitCases(std::string yaml_path) {
    std::vector<SQLCase> cases;
    InitCases(yaml_path, cases);
    return cases;
}
class MockClosure : public ::google::protobuf::Closure {
 public:
    MockClosure() {}
    ~MockClosure() {}
    void Run() {}
};
class DBMSSdkTest : public ::testing::TestWithParam<SQLCase> {
 public:
    DBMSSdkTest()
        : dbms_server_(), tablet_server_(), tablet_(NULL), dbms_(NULL) {}
    ~DBMSSdkTest() {}
    void SetUp() {
        brpc::ServerOptions options;
        tablet_ = new tablet::TabletServerImpl();
        tablet_->Init();
        tablet_server_.AddService(tablet_, brpc::SERVER_DOESNT_OWN_SERVICE);
        tablet_server_.Start(tablet_port, &options);
        dbms_ = new ::fesql::dbms::DBMSServerImpl();
        dbms_server_.AddService(dbms_, brpc::SERVER_DOESNT_OWN_SERVICE);
        dbms_server_.Start(dbms_port, &options);
        {
            std::string tablet_endpoint =
                "127.0.0.1:" + std::to_string(tablet_port);
            MockClosure closure;
            dbms::KeepAliveRequest request;
            request.set_endpoint(tablet_endpoint);
            dbms::KeepAliveResponse response;
            dbms_->KeepAlive(NULL, &request, &response, &closure);
        }
    }

    void TearDown() {
        dbms_server_.Stop(10);
        tablet_server_.Stop(10);
        delete tablet_;
        delete dbms_;
    }

 public:
    brpc::Server dbms_server_;
    brpc::Server tablet_server_;
    int tablet_port = 7212;
    int dbms_port = 7211;
    tablet::TabletServerImpl *tablet_;
    dbms::DBMSServerImpl *dbms_;
};

TEST_F(DBMSSdkTest, DatabasesAPITest) {
    usleep(2000 * 1000);
    const std::string endpoint = "127.0.0.1:" + std::to_string(dbms_port);
    std::shared_ptr<::fesql::sdk::DBMSSdk> dbms_sdk =
        ::fesql::sdk::CreateDBMSSdk(endpoint);
    {
        Status status;
        std::vector<std::string> names = dbms_sdk->GetDatabases(&status);
        ASSERT_EQ(0, static_cast<int>(status.code));
        ASSERT_EQ(0u, names.size());
    }

    // create database db1
    {
        Status status;
        std::string name = "db_1";
        dbms_sdk->CreateDatabase(name, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }
    // create database db2
    {
        Status status;
        std::string name = "db_2xxx";
        dbms_sdk->CreateDatabase(name, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }

    // create database db3
    {
        Status status;
        std::string name = "db_3";
        dbms_sdk->CreateDatabase(name, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }

    {
        // get databases
        Status status;
        std::vector<std::string> names = dbms_sdk->GetDatabases(&status);
        ASSERT_EQ(0, static_cast<int>(status.code));
        ASSERT_EQ(3u, names.size());
    }
}

TEST_F(DBMSSdkTest, TableAPITest) {
    usleep(2000 * 1000);
    const std::string endpoint = "127.0.0.1:" + std::to_string(dbms_port);
    std::shared_ptr<::fesql::sdk::DBMSSdk> dbms_sdk =
        ::fesql::sdk::CreateDBMSSdk(endpoint);
    // create database db1
    {
        Status status;
        std::string name = "db_1";
        dbms_sdk->CreateDatabase(name, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }

    // create database db2
    {
        Status status;
        std::string name = "db_2x";
        dbms_sdk->CreateDatabase(name, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }
    {
        Status status;
        // create table test1
        std::string sql =
            "create table test1(\n"
            "    column1 int NOT NULL,\n"
            "    column2 timestamp NOT NULL,\n"
            "    column3 int,\n"
            "    column4 string NOT NULL,\n"
            "    column5 int,\n"
            "    index(key=(column4, column3), ts=column2, ttl=60d)\n"
            ");";
        std::string name = "db_1";
        dbms_sdk->ExecuteQuery(name, sql, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }
    {
        // create table test2
        std::string sql =
            "create table IF NOT EXISTS test2(\n"
            "    column1 int NOT NULL,\n"
            "    column2 timestamp NOT NULL,\n"
            "    column3 int NOT NULL,\n"
            "    column4 string NOT NULL,\n"
            "    column5 int NOT NULL,\n"
            "    index(key=(column1), ts=column2)\n"
            ");";

        std::string name = "db_1";
        Status status;
        dbms_sdk->ExecuteQuery(name, sql, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }

    {
        // create table test3
        std::string sql =
            "create table test3(\n"
            "    column1 int NOT NULL,\n"
            "    column2 timestamp NOT NULL,\n"
            "    column3 int NOT NULL,\n"
            "    column4 string NOT NULL,\n"
            "    column5 int NOT NULL,\n"
            "    index(key=(column4), ts=column2)\n"
            ");";

        std::string name = "db_1";
        Status status;
        dbms_sdk->ExecuteQuery(name, sql, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }
    {
        // show db_1 tables
        std::string name = "db_1";
        Status status;
        std::shared_ptr<TableSet> tablet_set =
            dbms_sdk->GetTables(name, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
        ASSERT_EQ(3u, tablet_set->Size());
    }
    {
        // show tables empty
        std::string name = "db_2x";
        Status status;
        std::shared_ptr<TableSet> ts = dbms_sdk->GetTables(name, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
        ASSERT_EQ(0u, ts->Size());
    }
}

TEST_F(DBMSSdkTest, GetInputSchema_ns_not_exist) {
    usleep(2000 * 1000);
    const std::string endpoint = "127.0.0.1:" + std::to_string(dbms_port);
    std::shared_ptr<::fesql::sdk::DBMSSdk> dbms_sdk =
        ::fesql::sdk::CreateDBMSSdk(endpoint);
    std::string name = "db_x123";
    {
        Status status;
        // select
        std::string sql = "select column1 from test3;";
        dbms_sdk->GetInputSchema(name, sql, &status);
        ASSERT_EQ(31, static_cast<int>(status.code));
    }
}

TEST_F(DBMSSdkTest, request_mode) {
    usleep(2000 * 1000);
    const std::string endpoint = "127.0.0.1:" + std::to_string(dbms_port);
    std::shared_ptr<::fesql::sdk::DBMSSdk> dbms_sdk =
        ::fesql::sdk::CreateDBMSSdk(endpoint);
    std::string name = "db_x123";
    {
        Status status;
        dbms_sdk->CreateDatabase(name, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }

    {
        Status status;
        // create table db1
        std::string sql =
            "create table test3(\n"
            "    column1 int NOT NULL,\n"
            "    column2 bigint NOT NULL,\n"
            "    column3 int NOT NULL,\n"
            "    column4 string NOT NULL,\n"
            "    column5 int NOT NULL,\n"
            "    index(key=column4, ts=column2)\n"
            ");";
        dbms_sdk->ExecuteQuery(name, sql, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }
    {
        Status status;
        // insert
        std::string sql = "insert into test3 values(1, 4000, 2, \"hello\", 3);";
        dbms_sdk->ExecuteQuery(name, sql, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }
    {
        Status status;
        // select
        std::string sql = "select column1 + 5 from test3;";
        std::shared_ptr<RequestRow> row =
            dbms_sdk->GetRequestRow(name, sql, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
        std::string column4 = "hello";
        ASSERT_EQ(5, row->GetSchema()->GetColumnCnt());
        ASSERT_TRUE(row->Init(column4.size()));
        ASSERT_TRUE(row->AppendInt32(32));
        ASSERT_TRUE(row->AppendInt64(64));
        ASSERT_TRUE(row->AppendInt32(32));
        ASSERT_TRUE(row->AppendString(column4));
        ASSERT_TRUE(row->AppendInt32(32));
        ASSERT_TRUE(row->Build());
        std::shared_ptr<ResultSet> rs =
            dbms_sdk->ExecuteQuery(name, sql, row, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
        ASSERT_EQ(1, rs->Size());
        ASSERT_TRUE(rs->Next());
        ASSERT_EQ(37, rs->GetInt32Unsafe(0));
    }
}

TEST_F(DBMSSdkTest, GetInputSchema_table_not_exist) {
    usleep(2000 * 1000);
    const std::string endpoint = "127.0.0.1:" + std::to_string(dbms_port);
    std::shared_ptr<::fesql::sdk::DBMSSdk> dbms_sdk =
        ::fesql::sdk::CreateDBMSSdk(endpoint);

    std::string name = "db_x12";
    {
        Status status;
        dbms_sdk->CreateDatabase(name, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }
    {
        Status status;
        // select
        std::string sql = "select column1 from test3;";
        dbms_sdk->GetInputSchema(name, sql, &status);
        ASSERT_EQ(31, static_cast<int>(status.code));
    }
}

TEST_F(DBMSSdkTest, GetInputSchema1) {
    usleep(2000 * 1000);
    const std::string endpoint = "127.0.0.1:" + std::to_string(dbms_port);
    std::shared_ptr<::fesql::sdk::DBMSSdk> dbms_sdk =
        ::fesql::sdk::CreateDBMSSdk(endpoint);

    std::string name = "db_x11";
    {
        Status status;
        dbms_sdk->CreateDatabase(name, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }
    {
        Status status;
        // create table db1
        std::string sql =
            "create table test3(\n"
            "    column1 int NOT NULL,\n"
            "    column2 bigint NOT NULL,\n"
            "    column3 int NOT NULL,\n"
            "    column4 string NOT NULL,\n"
            "    column5 int NOT NULL,\n"
            "    index(key=column4, ts=column2)\n"
            ");";
        dbms_sdk->ExecuteQuery(name, sql, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }
    {
        Status status;
        // select
        std::string sql = "select column1 from test3;";
        const Schema &schema = dbms_sdk->GetInputSchema(name, sql, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
        ASSERT_EQ(5, schema.GetColumnCnt());
    }
}

TEST_F(DBMSSdkTest, ExecuteSQLTest) {
    usleep(2000 * 1000);
    const std::string endpoint = "127.0.0.1:" + std::to_string(dbms_port);
    std::shared_ptr<::fesql::sdk::DBMSSdk> dbms_sdk =
        ::fesql::sdk::CreateDBMSSdk(endpoint);
    std::string name = "db_2";
    {
        Status status;
        dbms_sdk->CreateDatabase(name, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }
    {
        Status status;
        // create table db1
        std::string sql =
            "create table test3(\n"
            "    column1 int NOT NULL,\n"
            "    column2 bigint NOT NULL,\n"
            "    column3 int NOT NULL,\n"
            "    column4 string NOT NULL,\n"
            "    column5 int NOT NULL,\n"
            "    index(key=column4, ts=column2)\n"
            ");";
        dbms_sdk->ExecuteQuery(name, sql, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }
    {
        Status status;
        // insert
        std::string sql = "insert into test3 values(1, 4000, 2, \"hello\", 3);";
        dbms_sdk->ExecuteQuery(name, sql, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }
    {
        Status status;
        std::string sql =
            "select column1, column2, column3, column4, column5 from test3 "
            "limit 1;";
        std::shared_ptr<ResultSet> rs =
            dbms_sdk->ExecuteQuery(name, sql, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
        if (rs) {
            const Schema *schema = rs->GetSchema();
            ASSERT_EQ(5, schema->GetColumnCnt());
            ASSERT_EQ("column1", schema->GetColumnName(0));
            ASSERT_EQ("column2", schema->GetColumnName(1));
            ASSERT_EQ("column3", schema->GetColumnName(2));
            ASSERT_EQ("column4", schema->GetColumnName(3));
            ASSERT_EQ("column5", schema->GetColumnName(4));

            ASSERT_EQ(kTypeInt32, schema->GetColumnType(0));
            ASSERT_EQ(kTypeInt64, schema->GetColumnType(1));
            ASSERT_EQ(kTypeInt32, schema->GetColumnType(2));
            ASSERT_EQ(kTypeString, schema->GetColumnType(3));
            ASSERT_EQ(kTypeInt32, schema->GetColumnType(4));

            ASSERT_TRUE(rs->Next());
            {
                int32_t val = 0;
                ASSERT_TRUE(rs->GetInt32(0, &val));
                ASSERT_EQ(val, 1);
            }
            {
                int64_t val = 0;
                ASSERT_TRUE(rs->GetInt64(1, &val));
                ASSERT_EQ(val, 4000);
            }
            {
                int32_t val = 0;
                ASSERT_TRUE(rs->GetInt32(2, &val));
                ASSERT_EQ(val, 2);
            }

            {
                int val = 0;
                ASSERT_TRUE(rs->GetInt32(4, &val));
                ASSERT_EQ(val, 3);
            }

            {
                std::string val;
                ASSERT_TRUE(rs->GetString(3, &val));
                ASSERT_EQ(val, "hello");
            }
        } else {
            ASSERT_FALSE(true);
        }
    }
}

TEST_F(DBMSSdkTest, ExecuteScriptAPITest) {
    usleep(2000 * 1000);
    const std::string endpoint = "127.0.0.1:" + std::to_string(dbms_port);
    std::shared_ptr<::fesql::sdk::DBMSSdk> dbms_sdk =
        ::fesql::sdk::CreateDBMSSdk(endpoint);

    {
        Status status;
        std::string name = "db_1";
        dbms_sdk->CreateDatabase(name, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }

    {
        // create table db1
        std::string sql =
            "create table test3(\n"
            "    column1 int NOT NULL,\n"
            "    column2 timestamp NOT NULL,\n"
            "    column3 int NOT NULL,\n"
            "    column4 string NOT NULL,\n"
            "    column5 int NOT NULL,\n"
            "    index(key=(column4), ts=column2)\n"
            ");";

        std::string name = "db_1";
        Status status;
        dbms_sdk->ExecuteQuery(name, sql, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }

    {
        // create table db1
        std::string sql =
            "create table test4(\n"
            "    column1 int NOT NULL,\n"
            "    column2 timestamp NOT NULL,\n"
            "    index(key=(column1), ts=column2)\n"
            ");";

        std::string name = "db_1";
        Status status;
        dbms_sdk->ExecuteQuery(name, sql, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }
}

void PrintRows(const vm::Schema &schema, const std::vector<codec::Row> &rows) {
    std::ostringstream oss;
    codec::RowView row_view(schema);
    ::fesql::base::TextTable t('-', '|', '+');
    // Add Header
    for (int i = 0; i < schema.size(); i++) {
        t.add(schema.Get(i).name());
        if (t.current_columns_size() >= 20) {
            t.add("...");
            break;
        }
    }
    t.endOfRow();
    if (rows.empty()) {
        t.add("Empty set");
        t.endOfRow();
        return;
    }

    for (auto row : rows) {
        row_view.Reset(row.buf());
        for (int idx = 0; idx < schema.size(); idx++) {
            std::string str = row_view.GetAsString(idx);
            t.add(str);
            if (t.current_columns_size() >= 20) {
                t.add("...");
                break;
            }
        }
        t.endOfRow();
        if (t.rows().size() > 10) {
            break;
        }
    }
    oss << t << std::endl;
    LOG(INFO) << "\n" << oss.str() << "\n";
}

void PrintResultSet(std::shared_ptr<ResultSet> rs) {
    std::ostringstream oss;
    ::fesql::base::TextTable t('-', '|', '+');
    auto schema = rs->GetSchema();
    // Add Header
    for (int i = 0; i < schema->GetColumnCnt(); i++) {
        t.add(schema->GetColumnName(i));
        if (t.current_columns_size() >= 20) {
            t.add("...");
            break;
        }
    }
    t.endOfRow();
    if (0 == rs->Size()) {
        t.add("Empty set");
        t.endOfRow();
        return;
    }

    while (rs->Next()) {
        for (int idx = 0; idx < schema->GetColumnCnt(); idx++) {
            std::string str = rs->GetAsString(idx);
            t.add(str);
            if (t.current_columns_size() >= 20) {
                t.add("...");
                break;
            }
        }
        t.endOfRow();
        if (t.rows().size() > 10) {
            break;
        }
    }
    oss << t << std::endl;
    LOG(INFO) << "\n" << oss.str() << "\n";
}
void CheckRows(const vm::Schema &schema, const std::string &order_col,
               const std::vector<codec::Row> &rows,
               std::shared_ptr<ResultSet> rs) {
    ASSERT_EQ(rows.size(), rs->Size());

    LOG(INFO) << "Expected Rows: \n";
    PrintRows(schema, rows);
    LOG(INFO) << "ResultSet Rows: \n";
    PrintResultSet(rs);
    codec::RowView row_view(schema);
    int order_idx = -1;
    for (int i = 0; i < schema.size(); i++) {
        if (schema.Get(i).name() == order_col) {
            order_idx = i;
            break;
        }
    }
    std::map<std::string, codec::Row> rows_map;
    for (auto row : rows) {
        row_view.Reset(row.buf());
        std::string key = row_view.GetAsString(order_idx);
        rows_map.insert(std::make_pair(key, row));
    }
    int32_t index = 0;
    rs->Reset();
    while (rs->Next()) {
        if (order_idx) {
            std::string key = rs->GetAsString(order_idx);
            row_view.Reset(rows_map[key].buf());
        } else {
            row_view.Reset(rows[index++].buf());
        }
        for (int i = 0; i < schema.size(); i++) {
            if (row_view.IsNULL(i)) {
                ASSERT_TRUE(rs->IsNULL(i)) << " At " << i;
                continue;
            }
            switch (schema.Get(i).type()) {
                case fesql::type::kInt32: {
                    ASSERT_EQ(row_view.GetInt32Unsafe(i), rs->GetInt32Unsafe(i))
                        << " At " << i;
                    break;
                }
                case fesql::type::kInt64: {
                    ASSERT_EQ(row_view.GetInt64Unsafe(i), rs->GetInt64Unsafe(i))
                        << " At " << i;
                    break;
                }
                case fesql::type::kInt16: {
                    ASSERT_EQ(row_view.GetInt16Unsafe(i), rs->GetInt16Unsafe(i))
                        << " At " << i;
                    break;
                }
                case fesql::type::kFloat: {
                    ASSERT_FLOAT_EQ(row_view.GetFloatUnsafe(i),
                                    rs->GetFloatUnsafe(i))
                        << " At " << i;
                    break;
                }
                case fesql::type::kDouble: {
                    ASSERT_DOUBLE_EQ(row_view.GetDoubleUnsafe(i),
                                     rs->GetDoubleUnsafe(i))
                        << " At " << i;
                    break;
                }
                case fesql::type::kVarchar: {
                    ASSERT_EQ(row_view.GetStringUnsafe(i),
                              rs->GetStringUnsafe(i))
                        << " At " << i;
                    break;
                }
                case fesql::type::kTimestamp: {
                    ASSERT_EQ(row_view.GetTimestampUnsafe(i),
                              rs->GetTimeUnsafe(i))
                        << " At " << i;
                    break;
                }
                case fesql::type::kDate: {
                    ASSERT_EQ(row_view.GetDateUnsafe(i), rs->GetDateUnsafe(i))
                        << " At " << i;
                    break;
                }
                case fesql::type::kBool: {
                    ASSERT_EQ(row_view.GetBoolUnsafe(i), rs->GetBoolUnsafe(i))
                        << " At " << i;
                    break;
                }
                default: {
                    FAIL() << "Invalid Column Type";
                    break;
                }
            }
        }
    }
}
INSTANTIATE_TEST_CASE_P(
    SdkInsert, DBMSSdkTest,
    testing::ValuesIn(InitCases("/cases/insert/simple_insert.yaml")));

TEST_P(DBMSSdkTest, ExecuteQueryTest) {
    auto sql_case = GetParam();
    usleep(1000 * 1000);
    const std::string endpoint = "127.0.0.1:" + std::to_string(dbms_port);
    std::shared_ptr<::fesql::sdk::DBMSSdk> dbms_sdk =
        ::fesql::sdk::CreateDBMSSdk(endpoint);

    std::string db = sql_case.db();
    {
        Status status;
        dbms_sdk->CreateDatabase(db, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
    }
    {
        // create and insert inputs
        for (size_t i = 0; i < sql_case.inputs().size(); i++) {
            if (sql_case.inputs()[i].name_.empty()) {
                sql_case.set_input_name(SQLCase::GenRand("auto_t"), i);
            }
            Status status;
            std::string create;
            ASSERT_TRUE(sql_case.BuildCreateSQLFromInput(i, &create));
            std::string placeholder = "{" + std::to_string(i) + "}";
            boost::replace_all(create, placeholder, sql_case.inputs()[i].name_);
            LOG(INFO) << create;
            dbms_sdk->ExecuteQuery(db, create, &status);
            ASSERT_EQ(0, static_cast<int>(status.code));

            std::string insert;
            ASSERT_TRUE(sql_case.BuildInsertSQLFromInput(i, &insert));
            boost::replace_all(insert, placeholder, sql_case.inputs()[i].name_);
            LOG(INFO) << insert;
            dbms_sdk->ExecuteQuery(db, insert, &status);
            ASSERT_EQ(0, static_cast<int>(status.code));
        }
    }
    {
        Status status;
        std::string sql = sql_case.sql_str();
        for (size_t i = 0; i < sql_case.inputs().size(); i++) {
            std::string placeholder = "{" + std::to_string(i) + "}";
            boost::replace_all(sql, placeholder, sql_case.inputs()[i].name_);
        }
        LOG(INFO) << sql;
        std::shared_ptr<ResultSet> rs =
            dbms_sdk->ExecuteQuery(db, sql, &status);
        ASSERT_EQ(0, static_cast<int>(status.code));
        std::vector<codec::Row> rows;
        sql_case.ExtractOutputData(rows);
        type::TableDef output_table;
        sql_case.ExtractOutputSchema(output_table);
        CheckRows(output_table.columns(), sql_case.expect().order_, rows, rs);
    }
}

}  // namespace sdk
}  // namespace fesql
int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    InitLLVM X(argc, argv);
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    ::google::ParseCommandLineFlags(&argc, &argv, true);
    FLAGS_enable_keep_alive = false;
    return RUN_ALL_TESTS();
}
