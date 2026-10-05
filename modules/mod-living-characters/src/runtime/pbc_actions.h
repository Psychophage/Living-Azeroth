// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Living Azeroth: finite action scheduling, independent of dialogue playback.
#ifndef PBC_ACTIONS_H
#define PBC_ACTIONS_H

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "PlayerbotDialogueTypes.h"

namespace PBC
{
using ActionRequest = PlayerbotDialogue::Request;
using ActionOutcome = PlayerbotDialogue::Outcome;
using ActionStatus = PlayerbotDialogue::Status;
using ActionKind = PlayerbotDialogue::Kind;

std::map<std::string, uint64_t> ActionMoneyAmounts(std::string const& contribution);
std::string ActionName(ActionKind kind);
std::optional<ActionKind> ParseActionName(std::string const& name);
bool ChangesMovement(ActionKind kind);
bool SensitiveAction(ActionKind kind);
bool SameActionIntent(ActionRequest const& first, ActionRequest const& second);

// The journal callback must durably accept sensitive intent BEFORE native
// dispatch. Pending means the async write has not finished. A failed/uncertain
// write never dispatches.
struct ActionCallbacks
{
    std::function<ActionStatus(ActionRequest const&)> prepare;
    std::function<bool(ActionRequest const&)> dispatch;
    std::function<void(ActionRequest const&)> cancel;
    std::function<void(ActionRequest const&, ActionOutcome const&)> observe;
};

class ActionCoordinator
{
public:
    explicit ActionCoordinator(ActionCallbacks callbacks);
    // At most two intents for one bot. IDs and sequences come from admitted human
    // input.
    bool Submit(std::vector<ActionRequest> requests, uint64_t nowMs);
    void Update(uint64_t nowMs);
    void Receive(ActionOutcome const& outcome);
    void CancelActor(uint64_t actor, std::string const& reason, uint64_t nowMs);
    void CancelAll(std::string const& reason, uint64_t nowMs);
    bool Active(uint64_t actor) const { return _tasks.contains(actor); }
    std::size_t Active() const { return _tasks.size(); }

private:
    struct Task
    {
        ActionRequest current;
        std::optional<ActionRequest> next;
        ActionStatus status = ActionStatus::Pending;
        bool dispatched = false;
        uint64_t notBeforeMs = 0;
    };
    void Finish(std::map<uint64_t, Task>::iterator task, ActionOutcome const& outcome);
    ActionCallbacks _callbacks;
    std::map<uint64_t, Task> _tasks;
    // Lifetime deduplication uses monotonic admitted-input sequences, not a
    // growing ID set.
    std::map<uint64_t, uint64_t> _latestSequence;
    std::map<std::string, ActionRequest> _awaitingSettlement;
    struct Preparation
    {
        uint64_t deadlineMs = 0;
        std::map<uint64_t, ActionStatus> actors;
    };
    std::map<std::string, Preparation> _preparations;
};
}  // namespace PBC

#endif
