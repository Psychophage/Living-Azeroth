// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Living Azeroth: durable intent before dispatch; uncertain operations never replay.
#ifndef PBC_ACTION_STORE_H
#define PBC_ACTION_STORE_H

#include <memory>

#include "pbc_actions.h"

namespace PBC
{
struct ActionMoneyBalance
{
    uint64_t limit = 0;
    uint64_t spent = 0;
    uint64_t held = 0;
};
class ActionStore
{
public:
    ActionStore();
    ~ActionStore();
    bool Open(std::string const& connectionInfo);
    bool Prepare(ActionRequest const& request, std::string const& run, uint64_t epochMs);
    bool Record(ActionOutcome const& outcome, uint64_t epochMs);
    bool Recover(std::string const& currentRun, uint64_t epochMs);
    std::optional<ActionStatus> Status(std::string const& operation);
    std::optional<ActionMoneyBalance> MoneyBalance(std::string const& budget);

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};
}  // namespace PBC

#endif
