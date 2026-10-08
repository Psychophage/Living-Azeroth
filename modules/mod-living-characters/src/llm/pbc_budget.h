// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Living Azeroth changes; upstream PBC/AzerothCore authorship is retained.
#ifndef PBC_BUDGET_H
#define PBC_BUDGET_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace PBC
{
struct ApiReservation
{
    std::string requestId;
    std::string reason;
    std::string actorId;
    std::string sceneId;
    std::string model;
    std::string provider;
    uint32_t mapId = 0;
    uint32_t instanceId = 0;
    uint32_t zoneId = 0;
    uint16_t attempt = 1;
    uint64_t maximumNano = 0;
    bool background = false;
};

struct ApiUsage
{
    std::optional<uint64_t> actualNano;
    std::string providerRequestId;
    std::string usageJson = "null";
    uint32_t latencyMs = 0;
};

struct SceneCost
{
    std::string reason;  // dialogue, selector, memory, ...
    uint64_t nano = 0;
    uint32_t requests = 0;
};

struct BudgetTotals
{
    uint64_t ceilingNano = 0;
    uint64_t spentNano = 0;
    uint64_t heldNano = 0;
    uint64_t backgroundFloorNano = 0;
};

// Synchronous and thread-safe: call on model/storage workers, never per game tick.
// Shares the tables and lock order used by tools/pbc_budget.py. No budget creation,
// reset or ceiling increase is possible through this interface.
class ApiLedger
{
public:
    virtual ~ApiLedger() = default;
    virtual bool Reserve(ApiReservation const& request) = 0;
    virtual bool MarkDispatched(std::string const& requestId) = 0;
    virtual bool CancelUnsent(std::string const& requestId) = 0;
    virtual bool Reconcile(std::string const& requestId, ApiUsage const& usage) = 0;
    virtual bool RecordDelivery(std::string const& requestId, std::string const& outcome) = 0;
    virtual std::optional<BudgetTotals> Totals() = 0;
};

class BudgetStore final : public ApiLedger
{
public:
    BudgetStore();
    ~BudgetStore() override;
    bool Open(std::string const& connectionInfo, std::string const& budgetId);
    bool Reserve(ApiReservation const& request) override;
    bool MarkDispatched(std::string const& requestId) override;
    bool CancelUnsent(std::string const& requestId) override;
    bool Reconcile(std::string const& requestId, ApiUsage const& usage) override;
    bool RecordDelivery(std::string const& requestId, std::string const& outcome) override;
    std::optional<BudgetTotals> Totals() override;
    // What one scene's model requests cost against this budget, by reason.
    std::vector<SceneCost> CostOfScene(std::string const& sceneId);

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};
}

#endif
