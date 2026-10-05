// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
#include "pbc_actions.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <regex>
#include <tuple>
#include <utility>

namespace PBC
{
namespace
{
constexpr std::array Names = {"follow",        "stay",           "approach",   "regroup",
                              "retreat",       "formation",      "resume",     "attack",
                              "pull",          "stop_attack",    "assist",     "protect",
                              "buff",          "heal",           "cleanse",    "resurrect",
                              "pet_attack",    "pet_follow",     "pet_stay",   "pet_passive",
                              "pet_defensive", "pet_aggressive", "inspect",    "use",
                              "equip",         "trade",          "train",      "repair",
                              "buy",           "role",           "spec",       "talents",
                              "share_quest",   "accept_quest",   "quest_help", "invite",
                              "join",          "leave",          "duel",       "offer_supplies",
                              "decline_offer", "cancel",         "unequip",    "restore_equipment",
                              "dance",         "stop_dance"};
static_assert(Names.size() == static_cast<unsigned>(ActionKind::StopDance) + 1);
}  // namespace

std::map<std::string, uint64_t> ActionMoneyAmounts(std::string const& contribution)
{
    static std::regex const pattern(
        R"(\b([0-9]+(?:\.[0-9]+)?|zero|one|two|three|four|five|six|seven|eight|nine|ten)\s*(gold|silver|copper|g|s|c)\b)",
        std::regex::icase);
    static std::map<std::string, uint32_t> const words = {{"zero", 0},  {"one", 1},  {"two", 2}, {"three", 3},
                                                          {"four", 4},  {"five", 5}, {"six", 6}, {"seven", 7},
                                                          {"eight", 8}, {"nine", 9}, {"ten", 10}};
    std::string text = contribution;
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    std::map<std::string, uint64_t> amounts;
    std::size_t start = 0, endOfPrevious = 0;
    uint64_t amount = 0;
    uint32_t previousMultiplier = 0;
    bool active = false;
    for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it)
    {
        auto number = (*it)[1].str();
        std::size_t pos = it->position();
        if (number.size() > 12 || (pos && (text[pos - 1] == '.' || text[pos - 1] == '-')))
            continue;
        auto unit = (*it)[2].str();
        uint32_t multiplier = unit.front() == 'g' ? 10000 : unit.front() == 's' ? 100 : 1;
        double raw = (words.contains(number) ? words.at(number) : std::stod(number)) * multiplier;
        if (raw > 2147483647 || std::abs(raw - std::round(raw)) > 0.00001)
            continue;
        auto separator = active ? text.substr(endOfPrevious, pos - endOfPrevious) : std::string();
        bool adjacent =
            active && multiplier < previousMultiplier && std::regex_match(separator, std::regex(R"(\s*(?:,|and)?\s*)"));
        if (!adjacent)
        {
            if (active && amount <= 2147483647)
                amounts[text.substr(start, endOfPrevious - start)] = amount;
            start = pos;
            amount = 0;
        }
        amount += uint64_t(std::round(raw));
        previousMultiplier = multiplier;
        endOfPrevious = pos + it->length();
        active = true;
    }
    if (active && amount <= 2147483647)
        amounts[text.substr(start, endOfPrevious - start)] = amount;
    return amounts;
}

std::string ActionName(ActionKind kind)
{
    auto index = static_cast<unsigned>(kind);
    return index < Names.size() ? Names[index] : "invalid";
}

std::optional<ActionKind> ParseActionName(std::string const& name)
{
    for (unsigned i = 0; i < Names.size(); ++i)
        if (name == Names[i])
            return static_cast<ActionKind>(i);
    return std::nullopt;
}

bool ChangesMovement(ActionKind kind)
{
    return kind <= ActionKind::Resume || kind == ActionKind::Cancel;
}

bool SensitiveAction(ActionKind kind)
{
    return kind == ActionKind::Use || kind == ActionKind::Equip || kind == ActionKind::Trade ||
           kind == ActionKind::Unequip || kind == ActionKind::RestoreEquipment || kind == ActionKind::Train ||
           kind == ActionKind::Repair || kind == ActionKind::Buy || kind == ActionKind::Talents ||
           kind == ActionKind::AcceptQuest || kind == ActionKind::Leave || kind == ActionKind::ShareQuest ||
           kind == ActionKind::Invite || kind == ActionKind::Join;
}

ActionCoordinator::ActionCoordinator(ActionCallbacks callbacks) : _callbacks(std::move(callbacks)) {}

bool SameActionIntent(ActionRequest const& first, ActionRequest const& second)
{
    auto entity = [](auto const& value)
    { return std::tie(value.guid, value.map, value.instance, value.entry, value.incarnation); };
    return entity(first.actor) == entity(second.actor) && entity(first.target) == entity(second.target) &&
           entity(first.subject) == entity(second.subject) && first.kind == second.kind &&
           first.objectId == second.objectId && first.quantity == second.quantity &&
           first.tradeQuantities == second.tradeQuantities && first.durationMs == second.durationMs &&
           first.offerId == second.offerId && first.trainingSpells == second.trainingSpells &&
           first.returnToParty == second.returnToParty && first.preparedParty.size() == second.preparedParty.size() &&
           std::equal(first.preparedParty.begin(), first.preparedParty.end(), second.preparedParty.begin(),
                      [&](auto const& a, auto const& b)
                      { return entity(a.actor) == entity(b.actor) && a.authorityVersion == b.authorityVersion; }) &&
           first.vendorSlot == second.vendorSlot && first.talentSpec == second.talentSpec &&
           first.talentPlan == second.talentPlan && first.items.size() == second.items.size() &&
           std::equal(first.items.begin(), first.items.end(), second.items.begin(),
                      [](auto const& a, auto const& b)
                      {
                          return std::tie(a.guid, a.entry, a.count, a.position, a.property) ==
                                 std::tie(b.guid, b.entry, b.count, b.position, b.property);
                      });
}

bool ActionCoordinator::Submit(std::vector<ActionRequest> requests, uint64_t nowMs)
{
    if (requests.empty() || requests.size() > 2)
        return false;
    auto const& first = requests.front();
    auto actor = first.actor.guid;
    if (!actor || !first.sequence || first.sequence <= _latestSequence[actor])
        return false;
    for (auto const& request : requests)
        if (request.id.empty() || request.actor.guid != actor || request.sequence != first.sequence ||
            request.requester.guid != first.requester.guid || request.deadlineMs <= nowMs ||
            request.groupId != first.groupId || request.authorityVersion != first.authorityVersion)
            return false;
    if (requests.size() == 2 && requests[0].id == requests[1].id)
        return false;
    // Independent model questions can repeat one instruction in both slots.
    // This also protects personal proposals: distinct IDs do not authorize a repeated expense.
    if (requests.size() == 2 && SameActionIntent(requests[0], requests[1]))
        requests.pop_back();
    if (first.kind == ActionKind::Formation && first.objectId == 9)
    {
        if (!_preparations.contains(first.groupId) && _preparations.size() >= 128)
            return false;
        auto& preparation = _preparations[first.groupId];
        preparation.deadlineMs = first.deadlineMs;
        preparation.actors[actor] = ActionStatus::Pending;
    }
    CancelActor(actor, "superseded", nowMs);
    _latestSequence[actor] = first.sequence;
    Task task;
    task.current = std::move(requests[0]);
    if (requests.size() == 2)
        task.next = std::move(requests[1]);
    _tasks.emplace(actor, std::move(task));
    return true;
}

void ActionCoordinator::Update(uint64_t nowMs)
{
    std::erase_if(_preparations, [nowMs](auto const& entry) { return nowMs >= entry.second.deadlineMs; });
    for (auto it = _tasks.begin(); it != _tasks.end();)
    {
        auto current = it++;
        auto& task = current->second;
        if (nowMs >= task.current.deadlineMs)
        {
            if (task.dispatched && _callbacks.cancel)
                _callbacks.cancel(task.current);
            Finish(current, {task.current.id,
                             task.dispatched && SensitiveAction(task.current.kind) ? ActionStatus::Unresolved
                                                                                   : ActionStatus::Cancelled,
                             "deadline", nowMs});
            continue;
        }
        if (task.dispatched)
            continue;
        if (nowMs < task.notBeforeMs)
            continue;
        if (task.current.returnToParty && !task.current.preparedParty.empty())
        {
            auto preparation = _preparations.find(task.current.groupId);
            bool ready = preparation != _preparations.end();
            bool failed = false;
            for (auto const& companion : task.current.preparedParty)
            {
                if (preparation == _preparations.end())
                    break;
                auto state = preparation->second.actors.find(companion.actor.guid);
                if (state == preparation->second.actors.end() || state->second != ActionStatus::Completed)
                    ready = false;
                if (state != preparation->second.actors.end() &&
                    (state->second == ActionStatus::Failed || state->second == ActionStatus::Cancelled ||
                     state->second == ActionStatus::Unresolved))
                    failed = true;
            }
            if (failed)
            {
                Finish(current, {task.current.id, ActionStatus::Failed, "party_preparation_incomplete", nowMs});
                continue;
            }
            if (!ready)
                continue;
        }
        auto prepared = _callbacks.prepare
                            ? _callbacks.prepare(task.current)
                            : (SensitiveAction(task.current.kind) ? ActionStatus::Failed : ActionStatus::Completed);
        if (prepared == ActionStatus::Pending)
            continue;
        if (prepared != ActionStatus::Completed)
        {
            Finish(current, {task.current.id, ActionStatus::Failed, "intent_not_persisted", nowMs});
            continue;
        }
        // Native dispatch must revalidate identity, authority and target after
        // journal I/O.
        if (!_callbacks.dispatch || !_callbacks.dispatch(task.current))
        {
            Finish(current, {task.current.id, ActionStatus::Failed, "dispatch_refused", nowMs});
            continue;
        }
        task.dispatched = true;
    }
}

void ActionCoordinator::Receive(ActionOutcome const& outcome)
{
    for (auto it = _tasks.begin(); it != _tasks.end(); ++it)
        if (it->second.current.id == outcome.id && it->second.dispatched)
        {
            if (PlayerbotDialogue::Terminal(outcome.status))
                Finish(it, outcome);
            else if (outcome.status == ActionStatus::Started && it->second.status != ActionStatus::Started)
            {
                it->second.status = outcome.status;
                if (_callbacks.observe)
                    _callbacks.observe(it->second.current, outcome);
            }
            return;
        }
    auto retired = _awaitingSettlement.find(outcome.id);
    if (retired != _awaitingSettlement.end() &&
        (outcome.status == ActionStatus::Completed || outcome.status == ActionStatus::Failed ||
         outcome.status == ActionStatus::Cancelled))
    {
        if (_callbacks.observe)
            _callbacks.observe(retired->second, outcome);
        _awaitingSettlement.erase(retired);
    }
}

void ActionCoordinator::Finish(std::map<uint64_t, Task>::iterator it, ActionOutcome const& outcome)
{
    auto task = std::move(it->second);
    _tasks.erase(it);
    if (task.current.kind == ActionKind::Formation && task.current.objectId == 9)
        if (auto preparation = _preparations.find(task.current.groupId); preparation != _preparations.end())
            preparation->second.actors[task.current.actor.guid] = outcome.status;
    if (outcome.status == ActionStatus::Unresolved)
    {
        // Retain enough context for a late authoritative native result. Evicted
        // entries remain unresolved in the durable journal and are never replayed.
        if (_awaitingSettlement.size() >= 1024)
            _awaitingSettlement.erase(_awaitingSettlement.begin());
        _awaitingSettlement.emplace(task.current.id, task.current);
    }
    if (_callbacks.observe)
        _callbacks.observe(task.current, outcome);
    if (outcome.status == ActionStatus::Completed && task.next)
    {
        Task next;
        next.current = std::move(*task.next);
        if (task.current.kind == ActionKind::Stay && task.current.durationMs)
        {
            next.notBeforeMs = outcome.occurredMs + task.current.durationMs;
            // The dispatch deadline is relative to the end of the requested wait.
            next.current.deadlineMs += task.current.durationMs;
        }
        // Revalidation happens on a subsequent Update, never in a completion
        // callback.
        _tasks.emplace(next.current.actor.guid, std::move(next));
    }
    else if (task.next && _callbacks.observe)
        _callbacks.observe(*task.next,
                           {task.next->id, ActionStatus::Cancelled, "prerequisite_not_completed", outcome.occurredMs});
}

void ActionCoordinator::CancelActor(uint64_t actor, std::string const& reason, uint64_t nowMs)
{
    auto it = _tasks.find(actor);
    if (it == _tasks.end())
        return;
    auto const& task = it->second;
    if (task.dispatched && _callbacks.cancel)
        _callbacks.cancel(task.current);
    Finish(it,
           {task.current.id,
            task.dispatched && SensitiveAction(task.current.kind) ? ActionStatus::Unresolved : ActionStatus::Cancelled,
            reason, nowMs});
}

void ActionCoordinator::CancelAll(std::string const& reason, uint64_t nowMs)
{
    while (!_tasks.empty())
        CancelActor(_tasks.begin()->first, reason, nowMs);
}
}  // namespace PBC
