// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
#include "pbc_action_store.h"

#include <mutex>

#include "pbc_json.h"
#include "pbc_sql.h"

namespace PBC
{
namespace
{
enum Statement : uint32
{
    IntentRead,
    IntentInsert,
    OutcomeUpdate,
    RecoverPending,
    MoneyBudgetCreate,
    MoneyBudgetRead,
    MoneyBudgetHold,
    MoneyBudgetSettle,
    MoneyCreate,
    MoneyRead,
    MoneySettle,
    Count
};

class Connection final : public SqlConnection
{
public:
    explicit Connection(MySQLConnectionInfo& info) : SqlConnection(info) {}

private:
    void DoPrepareStatements() override
    {
        m_stmts.resize(Count);
        PrepareStatement(IntentRead, "SELECT status FROM pbc_action WHERE operation_id=? FOR UPDATE", CONNECTION_SYNCH);
        PrepareStatement(IntentInsert,
                         "INSERT INTO pbc_action "
                         "(operation_id,run_id,scene_id,actor_guid,requester_guid,"
                         "input_sequence,intent_json,status,"
                         "created_ms,updated_ms) "
                         "VALUES (?,?,?,?,?,?,?,'prepared',?,?)",
                         CONNECTION_SYNCH);
        PrepareStatement(OutcomeUpdate,
                         "UPDATE pbc_action SET status=?,detail=?,updated_ms=? "
                         "WHERE operation_id=?",
                         CONNECTION_SYNCH);
        PrepareStatement(RecoverPending,
                         "UPDATE pbc_action SET "
                         "status='unresolved',detail='process_restarted',updated_ms=? "
                         "WHERE run_id<>? AND status IN ('prepared','started')",
                         CONNECTION_SYNCH);
        PrepareStatement(MoneyBudgetCreate,
                         "INSERT IGNORE INTO pbc_action_money_budget "
                         "(id,requester_guid,limit_copper) VALUES (?,?,?)",
                         CONNECTION_SYNCH);
        PrepareStatement(MoneyBudgetRead,
                         "SELECT limit_copper,spent_copper,held_copper,requester_guid FROM "
                         "pbc_action_money_budget "
                         "WHERE id=? FOR UPDATE",
                         CONNECTION_SYNCH);
        PrepareStatement(MoneyBudgetHold,
                         "UPDATE pbc_action_money_budget SET "
                         "held_copper=held_copper+? WHERE id=?",
                         CONNECTION_SYNCH);
        PrepareStatement(MoneyBudgetSettle,
                         "UPDATE pbc_action_money_budget SET "
                         "held_copper=held_copper-?,spent_copper=spent_copper+? "
                         "WHERE id=?",
                         CONNECTION_SYNCH);
        PrepareStatement(MoneyCreate,
                         "INSERT INTO pbc_action_money "
                         "(operation_id,budget_id,reserved_copper) VALUES (?,?,?)",
                         CONNECTION_SYNCH);
        PrepareStatement(MoneyRead,
                         "SELECT budget_id,reserved_copper,settled FROM "
                         "pbc_action_money WHERE operation_id=? FOR UPDATE",
                         CONNECTION_SYNCH);
        PrepareStatement(MoneySettle,
                         "UPDATE pbc_action_money SET spent_copper=?,settled=1 "
                         "WHERE operation_id=?",
                         CONNECTION_SYNCH);
    }
};

std::string Name(ActionStatus status)
{
    switch (status)
    {
        case ActionStatus::Pending:
            return "prepared";
        case ActionStatus::Started:
            return "started";
        case ActionStatus::Completed:
            return "completed";
        case ActionStatus::Failed:
            return "failed";
        case ActionStatus::Cancelled:
            return "cancelled";
        case ActionStatus::Unresolved:
            return "unresolved";
    }
    return "unresolved";
}

std::optional<ActionStatus> StatusFrom(std::string const& value)
{
    for (auto status : {ActionStatus::Pending, ActionStatus::Started, ActionStatus::Completed, ActionStatus::Failed,
                        ActionStatus::Cancelled, ActionStatus::Unresolved})
        if (Name(status) == value)
            return status;
    return std::nullopt;
}

pbc_json EntityJson(PlayerbotDialogue::Entity const& entity)
{
    return {{"guid", entity.guid},
            {"map", entity.map},
            {"instance", entity.instance},
            {"entry", entity.entry},
            {"incarnation", entity.incarnation}};
}
}  // namespace

class ActionStore::Impl
{
public:
    std::mutex mutex;
    std::unique_ptr<MySQLConnectionInfo> info;
    std::unique_ptr<Connection> connection;
    bool Ready()
    {
        if (!info)
            return false;
        if (!connection || connection->failed)
        {
            connection = std::make_unique<Connection>(*info);
            if (connection->Open() || !connection->PrepareStatements())
            {
                connection.reset();
                return false;
            }
        }
        return true;
    }
};

ActionStore::ActionStore() : _impl(std::make_unique<Impl>()) {}
ActionStore::~ActionStore() = default;

bool ActionStore::Open(std::string const& connectionInfo)
{
    std::lock_guard lock(_impl->mutex);
    _impl->connection.reset();
    _impl->info = std::make_unique<MySQLConnectionInfo>(connectionInfo);
    return _impl->Ready();
}

bool ActionStore::Prepare(ActionRequest const& request, std::string const& run, uint64_t epochMs)
{
    if (request.id.empty() || request.id.size() > 96 || request.groupId.size() > 64 || run.empty() || run.size() > 64)
        return false;
    pbc_json payload = {{"kind", ActionName(request.kind)},
                        {"actor", EntityJson(request.actor)},
                        {"requester", EntityJson(request.requester)},
                        {"subject", EntityJson(request.subject)},
                        {"target", EntityJson(request.target)},
                        {"authority_version", request.authorityVersion},
                        {"actor_group", request.actorGroup},
                        {"inviting_leader", request.invitingLeader},
                        {"offer_id", request.offerId},
                        {"object_id", request.objectId},
                        {"quantity", request.quantity},
                        {"trade_quantities", request.tradeQuantities},
                        {"duration_ms", request.durationMs},
                        {"copper_limit", request.copperLimit},
                        {"deadline_ms", request.deadlineMs},
                        {"voluntary", request.voluntary}};
    payload["money_budget"] = request.moneyBudget;
    payload["reserved_copper"] = request.reservedCopper;
    payload["training_spells"] = request.trainingSpells;
    payload["return_to_party"] = request.returnToParty;
    payload["prepared_party"] = pbc_json::array();
    for (auto const& companion : request.preparedParty)
        payload["prepared_party"].push_back({{"actor", EntityJson(companion.actor)},
                                           {"authority_version", companion.authorityVersion}});
    payload["vendor_slot"] = request.vendorSlot;
    payload["talent_spec"] = request.talentSpec;
    payload["talent_plan"] = pbc_json::array();
    for (auto const& rank : request.talentPlan)
        payload["talent_plan"].push_back({{"talent", rank.talent}, {"rank", rank.rank}});
    payload["items"] = pbc_json::array();
    for (auto const& item : request.items)
        payload["items"].push_back({{"guid", item.guid},
                                    {"entry", item.entry},
                                    {"count", item.count},
                                    {"position", item.position},
                                    {"property", item.property}});
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return false;
    auto& db = *_impl->connection;
    SqlTransaction transaction(db);
    if (!transaction || db.Read(IntentRead, request.id) || db.failed)
        return false;  // Existing IDs, including unresolved ones, cannot authorize a
                       // second dispatch.
    bool paidService = request.kind == ActionKind::Train || request.kind == ActionKind::Repair ||
                       request.kind == ActionKind::Buy || request.kind == ActionKind::Talents;
    if (paidService)
    {
        if (request.moneyBudget.empty() || request.moneyBudget.size() > 96 || request.copperLimit > 2147483647 ||
            request.reservedCopper > request.copperLimit ||
            !db.Write(MoneyBudgetCreate, request.moneyBudget, request.requester.guid, request.copperLimit))
            return false;
        auto budget = db.Read(MoneyBudgetRead, request.moneyBudget);
        if (!budget)
            return false;
        auto limit = (*budget)[0].Get<uint64_t>();
        auto spent = (*budget)[1].Get<uint64_t>();
        auto held = (*budget)[2].Get<uint64_t>();
        if (limit != request.copperLimit || (*budget)[3].Get<uint64_t>() != request.requester.guid || spent > limit ||
            held > limit - spent || request.reservedCopper > limit - spent - held ||
            !db.Write(MoneyBudgetHold, request.reservedCopper, request.moneyBudget) ||
            !db.Write(MoneyCreate, request.id, request.moneyBudget, request.reservedCopper))
            return false;
    }
    return db.Write(IntentInsert, request.id, run, request.groupId, request.actor.guid, request.requester.guid,
                    request.sequence, payload.dump(), epochMs, epochMs) &&
           transaction.Commit();
}

bool ActionStore::Record(ActionOutcome const& outcome, uint64_t epochMs)
{
    if (outcome.status == ActionStatus::Pending || outcome.detail.size() > 255)
        return false;
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return false;
    auto& db = *_impl->connection;
    SqlTransaction transaction(db);
    if (!transaction)
        return false;
    auto row = db.Read(IntentRead, outcome.id);
    if (!row)
        return false;
    auto previous = StatusFrom((*row)[0].Get<std::string>());
    if (!previous)
        return false;
    if (*previous == outcome.status)
        return transaction.Commit();
    if (PlayerbotDialogue::Terminal(*previous) && *previous != ActionStatus::Unresolved)
        return false;
    if (*previous == ActionStatus::Unresolved && outcome.status != ActionStatus::Completed &&
        outcome.status != ActionStatus::Failed && outcome.status != ActionStatus::Cancelled)
        return false;
    if (PlayerbotDialogue::Terminal(outcome.status) && outcome.status != ActionStatus::Unresolved)
    {
        auto money = db.Read(MoneyRead, outcome.id);
        if (db.failed)
            return false;
        if (money && !(*money)[2].Get<bool>())
        {
            auto budgetId = (*money)[0].Get<std::string>();
            auto reserved = (*money)[1].Get<uint64_t>();
            auto budget = db.Read(MoneyBudgetRead, budgetId);
            if (!budget || (*budget)[2].Get<uint64_t>() < reserved ||
                !db.Write(MoneyBudgetSettle, reserved, outcome.spentCopper, budgetId) ||
                !db.Write(MoneySettle, outcome.spentCopper, outcome.id))
                return false;
        }
    }
    return db.Write(OutcomeUpdate, Name(outcome.status), outcome.detail, epochMs, outcome.id) && transaction.Commit();
}

bool ActionStore::Recover(std::string const& currentRun, uint64_t epochMs)
{
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready() || currentRun.empty())
        return false;
    // No action is replayed and no resources are assumed returned. The next human
    // request must take a fresh snapshot of native inventory/currency/authority.
    return _impl->connection->Write(RecoverPending, epochMs, currentRun);
}

std::optional<ActionStatus> ActionStore::Status(std::string const& operation)
{
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return std::nullopt;
    auto row = _impl->connection->Read(IntentRead, operation);
    return row ? StatusFrom((*row)[0].Get<std::string>()) : std::nullopt;
}

std::optional<ActionMoneyBalance> ActionStore::MoneyBalance(std::string const& budget)
{
    std::lock_guard lock(_impl->mutex);
    if (!_impl->Ready())
        return std::nullopt;
    auto row = _impl->connection->Read(MoneyBudgetRead, budget);
    if (!row)
        return std::nullopt;
    return ActionMoneyBalance{(*row)[0].Get<uint64_t>(), (*row)[1].Get<uint64_t>(), (*row)[2].Get<uint64_t>()};
}
}  // namespace PBC
