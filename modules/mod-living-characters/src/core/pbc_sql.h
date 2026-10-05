// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Shared prepared-statement transaction handling for Living Azeroth stores.
#ifndef PBC_SQL_H
#define PBC_SQL_H

#include "MySQLConnection.h"
#include "MySQLPreparedStatement.h"
#include "PreparedStatement.h"
#include "QueryResult.h"
#include <memory>
#include <utility>

namespace PBC
{
class SqlConnection : public MySQLConnection
{
public:
    explicit SqlConnection(MySQLConnectionInfo& info) : MySQLConnection(info) { }
    bool failed = false;

    template<typename... Args>
    bool Write(uint32 index, Args&&... args)
    {
        if (failed)
            return false;
        auto statement = StatementFor(index, std::forward<Args>(args)...);
        return Execute(statement.get());
    }

    template<typename... Args>
    std::unique_ptr<PreparedResultSet> Read(uint32 index, Args&&... args)
    {
        if (failed)
            return nullptr;
        auto statement = StatementFor(index, std::forward<Args>(args)...);
        auto result = std::unique_ptr<PreparedResultSet>(Query(statement.get()));
        if (!result || !result->GetRowCount())
            return nullptr;
        return result;
    }

protected:
    // Replaying one statement after reconnecting could silently lose the transaction
    // and its locks. Reopen only between whole operations, through the owning store.
    bool _HandleMySQLErrno(uint32, char const*, uint8) override
    {
        failed = true;
        return false;
    }

private:
    template<typename... Args>
    std::unique_ptr<PreparedStatementBase> StatementFor(uint32 index, Args&&... args)
    {
        auto statement = std::make_unique<PreparedStatementBase>(index,
            static_cast<uint8>(m_stmts[index]->GetParameterCount()));
        if constexpr (sizeof...(Args) > 0)
            statement->SetArguments(std::forward<Args>(args)...);
        return statement;
    }
};

class SqlTransaction
{
public:
    explicit SqlTransaction(SqlConnection& connection) : _connection(connection)
    {
        _started = !_connection.failed && _connection.Execute("START TRANSACTION");
    }
    ~SqlTransaction()
    {
        if (_started && !_committed)
            _connection.Execute("ROLLBACK");
    }
    explicit operator bool() const { return _started; }
    bool Commit()
    {
        _committed = _started && !_connection.failed && _connection.Execute("COMMIT");
        return _committed;
    }

private:
    SqlConnection& _connection;
    bool _started = false;
    bool _committed = false;
};
}

#endif
