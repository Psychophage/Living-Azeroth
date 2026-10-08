// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Living Azeroth changes; uses AzerothCore's prepared-statement driver.
#include "pbc_budget.h"
#include "pbc_sql.h"
#include <mutex>
#include <set>
#include <utility>

namespace PBC
{
namespace
{
enum Statement : uint32_t
{
    LockBudget,
    ReadRequest,
    InsertRequest,
    HoldBudget,
    DispatchRequest,
    CancelRequest,
    ReleaseBudget,
    CompleteRequest,
    ChargeBudget,
    RecordUnknown,
    Delivery,
    SceneCostSelect,
    Count
};

class Connection final : public SqlConnection
{
public:
    explicit Connection(MySQLConnectionInfo& info) : SqlConnection(info) { }

private:
    void DoPrepareStatements() override
    {
        m_stmts.resize(Count);
        PrepareStatement(LockBudget, "SELECT ceiling_nano,spent_nano,held_nano,background_floor_nano "
            "FROM pbc_api_budget WHERE budget_id=? FOR UPDATE", CONNECTION_SYNCH);
        PrepareStatement(ReadRequest, "SELECT held_nano,state,actual_nano FROM pbc_api_request "
            "WHERE request_id=? AND budget_id=? FOR UPDATE", CONNECTION_SYNCH);
        PrepareStatement(InsertRequest, "INSERT INTO pbc_api_request "
            "(request_id,budget_id,reason,actor_id,scene_id,model,provider,"
            "map_id,instance_id,zone_id,attempt,held_nano) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?)", CONNECTION_SYNCH);
        PrepareStatement(HoldBudget, "UPDATE pbc_api_budget SET held_nano=held_nano+? WHERE budget_id=?",
            CONNECTION_SYNCH);
        PrepareStatement(DispatchRequest, "UPDATE pbc_api_request SET state='dispatched' WHERE request_id=?",
            CONNECTION_SYNCH);
        PrepareStatement(CancelRequest, "UPDATE pbc_api_request SET state='cancelled',held_nano=0,actual_nano=0 "
            "WHERE request_id=?", CONNECTION_SYNCH);
        PrepareStatement(ReleaseBudget, "UPDATE pbc_api_budget SET held_nano=held_nano-? WHERE budget_id=?",
            CONNECTION_SYNCH);
        PrepareStatement(CompleteRequest, "UPDATE pbc_api_request SET state='reconciled',held_nano=0,actual_nano=?,"
            "usage_json=?,provider_request_id=?,latency_ms=? WHERE request_id=?", CONNECTION_SYNCH);
        PrepareStatement(ChargeBudget, "UPDATE pbc_api_budget SET held_nano=held_nano-?,spent_nano=spent_nano+? "
            "WHERE budget_id=?", CONNECTION_SYNCH);
        PrepareStatement(RecordUnknown, "UPDATE pbc_api_request SET usage_json=?,provider_request_id=?,latency_ms=? "
            "WHERE request_id=?", CONNECTION_SYNCH);
        // What one scene cost, by kind of request; held amounts count until reconciled.
        PrepareStatement(SceneCostSelect, "SELECT reason,CAST(SUM(COALESCE(actual_nano,held_nano)) AS UNSIGNED),COUNT(*) "
            "FROM pbc_api_request WHERE budget_id=? AND scene_id=? AND state<>'cancelled' GROUP BY reason",
            CONNECTION_SYNCH);
        PrepareStatement(Delivery, "UPDATE pbc_api_request SET delivery=? WHERE request_id=? AND budget_id=?",
            CONNECTION_SYNCH);
    }
};

using Transaction = SqlTransaction;

}

class BudgetStore::Impl
{
public:
    std::mutex mutex;
    std::string budgetId;
    std::unique_ptr<MySQLConnectionInfo> info;
    std::unique_ptr<Connection> connection;

    bool Ready()
    {
        if (!info)
            return false;
        if (!connection || connection->failed)
        {
            connection.reset();
            connection = std::make_unique<Connection>(*info);
            if (connection->Open() != 0 || !connection->PrepareStatements())
            {
                connection.reset();
                return false;
            }
        }
        return true;
    }

    std::optional<BudgetTotals> Lock()
    {
        auto row = connection->Read(LockBudget, budgetId);
        if (!row)
            return std::nullopt;
        return BudgetTotals{(*row)[0].Get<uint64>(), (*row)[1].Get<uint64>(),
            (*row)[2].Get<uint64>(), (*row)[3].Get<uint64>()};
    }
};

BudgetStore::BudgetStore() : _impl(std::make_unique<Impl>()) { }
BudgetStore::~BudgetStore() = default;

bool BudgetStore::Open(std::string const& connectionInfo, std::string const& budgetId)
{
    std::lock_guard lock(_impl->mutex);
    _impl->connection.reset();
    _impl->info = std::make_unique<MySQLConnectionInfo>(connectionInfo);
    _impl->budgetId = budgetId;
    return _impl->Ready();
}

bool BudgetStore::Reserve(ApiReservation const& request)
{
    static std::set<std::string> const reasons =
        {"selector", "direct", "ambient", "combat_banter", "biography", "memory", "compaction", "evaluation"};
    if ((!request.maximumNano && request.provider != "self_hosted") || request.requestId.empty() || request.model.empty() ||
        !reasons.contains(request.reason) || request.attempt < 1 || request.attempt > 2)
        return false;
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return false;
    auto& db = *_impl->connection;
    Transaction transaction(db);
    if (!transaction)
        return false;
    auto totals = _impl->Lock();
    if (!totals || totals->spentNano > totals->ceilingNano)
        return false;
    uint64_t available = totals->ceilingNano - totals->spentNano;
    if (totals->heldNano > available)
        return false;
    available -= totals->heldNano;
    if (request.background)
    {
        if (totals->backgroundFloorNano > available)
            return false;
        available -= totals->backgroundFloorNano;
    }
    if (request.maximumNano > available || db.Read(ReadRequest, request.requestId, _impl->budgetId))
        return false;
    return db.Write(InsertRequest, request.requestId, _impl->budgetId, request.reason, request.actorId,
        request.sceneId, request.model, request.provider, request.mapId, request.instanceId, request.zoneId,
        request.attempt, request.maximumNano) &&
        db.Write(HoldBudget, request.maximumNano, _impl->budgetId) && transaction.Commit();
}

bool BudgetStore::MarkDispatched(std::string const& requestId)
{
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return false;
    auto& db = *_impl->connection;
    Transaction transaction(db);
    if (!transaction || !_impl->Lock())
        return false;
    auto row = db.Read(ReadRequest, requestId, _impl->budgetId);
    return row && (*row)[1].Get<std::string>() == "reserved" &&
        db.Write(DispatchRequest, requestId) && transaction.Commit();
}

bool BudgetStore::CancelUnsent(std::string const& requestId)
{
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return false;
    auto& db = *_impl->connection;
    Transaction transaction(db);
    if (!transaction || !_impl->Lock())
        return false;
    auto row = db.Read(ReadRequest, requestId, _impl->budgetId);
    if (!row || (*row)[1].Get<std::string>() != "reserved")
        return false;
    return db.Write(CancelRequest, requestId) &&
        db.Write(ReleaseBudget, (*row)[0].Get<uint64>(), _impl->budgetId) && transaction.Commit();
}

bool BudgetStore::Reconcile(std::string const& requestId, ApiUsage const& usage)
{
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return false;
    auto& db = *_impl->connection;
    Transaction transaction(db);
    if (!transaction || !_impl->Lock())
        return false;
    auto row = db.Read(ReadRequest, requestId, _impl->budgetId);
    if (!row)
        return false;
    std::string state = (*row)[1].Get<std::string>();
    if (state == "reconciled")
        return usage.actualNano && (*row)[2].Get<uint64>() == *usage.actualNano;
    if (state != "dispatched")
        return false;
    if (!usage.actualNano)
        return db.Write(RecordUnknown, usage.usageJson, usage.providerRequestId, usage.latencyMs, requestId) &&
            transaction.Commit();
    return db.Write(CompleteRequest, *usage.actualNano, usage.usageJson, usage.providerRequestId,
        usage.latencyMs, requestId) &&
        db.Write(ChargeBudget, (*row)[0].Get<uint64>(), *usage.actualNano, _impl->budgetId) && transaction.Commit();
}

bool BudgetStore::RecordDelivery(std::string const& requestId, std::string const& outcome)
{
    static std::set<std::string> const outcomes =
        {"delivered", "partial", "cancelled", "failed", "silent", "not_applicable", "stored", "discarded", "malformed"};
    if (!outcomes.contains(outcome))
        return false;
    std::lock_guard lock(_impl->mutex);
    return _impl->Ready() && _impl->connection->Write(Delivery, outcome, requestId, _impl->budgetId);
}

std::vector<SceneCost> BudgetStore::CostOfScene(std::string const& sceneId)
{
    std::vector<SceneCost> costs;
    std::lock_guard lock(_impl->mutex);
    if (sceneId.empty() || !_impl->Ready())
        return costs;
    if (auto rows = _impl->connection->Read(SceneCostSelect, _impl->budgetId, sceneId))
        do
        {
            costs.push_back({(*rows)[0].Get<std::string>(), (*rows)[1].Get<uint64_t>(), (*rows)[2].Get<uint32_t>()});
        } while (rows->NextRow());
    return costs;
}

std::optional<BudgetTotals> BudgetStore::Totals()
{
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return std::nullopt;
    Transaction transaction(*_impl->connection);
    if (!transaction)
        return std::nullopt;
    return _impl->Lock(); // Read-only transaction releases its lock on return.
}
}
