// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
#include "pbc_idle.h"

#include "pbc_character.h"
#include "pbc_event_dispatch.h"
#include "pbc_group_helpers.h"
#include "pbc_log.h"
#include "pbc_utils.h"

#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldSession.h"

#include <algorithm>
#include <deque>
#include <mutex>
#include <queue>
#include <random>
#include <unordered_map>

namespace
{
struct PartyView
{
    bool eligible = false;
    std::vector<uint64_t> members;
    std::vector<Player*> bots;
    std::vector<uint64_t> realPlayers;
};

struct IdleState
{
    std::vector<uint64_t> members;
    uint64_t incarnation = 0;
    uint64_t revision = 0;
    uint64_t reservation = 0;
    time_t reservationExpiresAt = 0;
    time_t nextAt = 0;
    time_t lastActivity = 0;
    time_t lastUnsafe = 0;
};

struct Completion
{
    PBC_IdleGuard guard;
    std::vector<PBC_HistoryEntry> replies;
};

std::mutex s_mutex;
std::unordered_map<uint32_t, IdleState> s_states;
std::queue<Completion> s_completions;
std::deque<time_t> s_callTimes;
uint64_t s_nextIdentity = 1;

time_t NextDelay()
{
    std::uniform_int_distribution<uint32_t> minutes(
        g_PBC_IdlePartyMinMinutes, g_PBC_IdlePartyMaxMinutes);
    return static_cast<time_t>(minutes(PBC_GetRNG())) * 60;
}

PartyView InspectParty(Group* group)
{
    PartyView view;
    if (!group || group->isRaidGroup())
        return view;

    bool safe = true;
    bool first = true;
    uint32_t mapId = 0;
    uint32_t instanceId = 0;
    uint32_t zoneId = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsInWorld() || !member->GetSession())
        {
            safe = false;
            continue;
        }

        view.members.push_back(member->GetGUID().GetCounter());
        if (member->GetSession()->IsBot())
            view.bots.push_back(member);
        else
            view.realPlayers.push_back(member->GetGUID().GetCounter());

        if (!member->IsAlive() || member->IsInCombat() || member->IsInFlight()
            || member->IsBeingTeleported())
            safe = false;

        if (first)
        {
            mapId = member->GetMapId();
            instanceId = member->GetInstanceId();
            zoneId = member->GetZoneId();
            first = false;
        }
        else if (mapId != member->GetMapId() || instanceId != member->GetInstanceId()
            || zoneId != member->GetZoneId())
            safe = false;
    }

    std::sort(view.members.begin(), view.members.end());
    view.eligible = safe && !view.bots.empty() && !view.realPlayers.empty()
        && view.members.size() == group->GetMembersCount();
    return view;
}

void NoteGroup(uint32_t groupCounter, time_t now)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_states.find(groupCounter);
    if (it == s_states.end())
        return;
    it->second.lastActivity = now;
    ++it->second.revision;
    it->second.reservation = 0;
    it->second.reservationExpiresAt = 0;
}

bool CurrentLocked(PBC_IdleGuard const& guard, time_t now)
{
    auto it = s_states.find(guard.groupCounter);
    if (!g_PBC_Enable || !g_PBC_IdlePartyEnabled || it == s_states.end())
        return false;
    IdleState const& state = it->second;
    return state.incarnation == guard.incarnation && state.revision == guard.revision
        && state.reservation == guard.reservation && guard.reservation != 0
        && state.members == guard.members && now <= guard.expiresAt;
}
} // namespace

void PBC_IdleNoteActivity(Player* player)
{
    if (!player || !g_PBC_IdlePartyEnabled)
        return;
    Group* group = player->GetGroup();
    if (group)
        NoteGroup(group->GetGUID().GetCounter(), std::time(nullptr));
}

void PBC_IdleNoteEvent(PBC_EventItem const& event)
{
    if (!g_PBC_IdlePartyEnabled || event.idleGuard.reservation != 0)
        return;

    std::unordered_set<uint32_t> groups;
    auto addGroup = [&groups](uint64_t guid)
    {
        Player* player = ObjectAccessor::FindPlayer(ObjectGuid(guid));
        if (player && player->GetGroup())
            groups.insert(player->GetGroup()->GetGUID().GetCounter());
    };

    for (uint64_t guid : event.playerCharGuids)
        addGroup(guid);
    for (PBC_CharacterSnapshot const& snap : event.respondingChars)
        addGroup(snap.charGuidRaw);
    for (uint64_t guid : event.silentCharGuids)
        addGroup(guid);

    time_t now = std::time(nullptr);
    for (uint32_t groupCounter : groups)
        NoteGroup(groupCounter, now);
}

void PBC_IdlePollParty(Group* group, time_t now)
{
    if (!g_PBC_Enable || !g_PBC_IdlePartyEnabled || !group)
        return;

    PartyView view = InspectParty(group);
    uint32_t groupCounter = group->GetGUID().GetCounter();
    PBC_IdleGuard guard;
    bool shouldDispatch = false;

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        IdleState& state = s_states[groupCounter];
        if (state.incarnation == 0 || state.members != view.members)
        {
            state = IdleState{};
            state.members = view.members;
            state.incarnation = s_nextIdentity++;
            state.nextAt = now + NextDelay();
            state.lastActivity = now;
            state.lastUnsafe = now;
        }

        if (!view.eligible)
        {
            state.lastUnsafe = now;
            if (state.reservation != 0)
            {
                ++state.revision;
                state.reservation = 0;
                state.reservationExpiresAt = 0;
            }
            return;
        }

        if (state.reservation != 0 && now > state.reservationExpiresAt)
        {
            ++state.revision;
            state.reservation = 0;
            state.reservationExpiresAt = 0;
        }

        if (state.reservation != 0 || now < state.nextAt
            || now - state.lastActivity < static_cast<time_t>(g_PBC_IdlePartyQuietSeconds)
            || now - state.lastUnsafe < 15)
            return;

        if (!g_PBC_EventThreadDone.load())
            return;
        {
            std::lock_guard<std::mutex> queueLock(g_PBC_EventQueueMutex);
            if (!g_PBC_EventQueue.empty())
                return;
        }
        {
            std::lock_guard<std::mutex> actionLock(g_PBC_PendingActionsMutex);
            if (!g_PBC_PendingActions.empty())
                return;
        }
        {
            std::lock_guard<std::mutex> requestLock(g_PBC_PendingEventRequestsMutex);
            if (!g_PBC_PendingEventRequests.empty())
                return;
        }
        if (!s_completions.empty())
            return;

        while (!s_callTimes.empty() && now - s_callTimes.front() >= 3600)
            s_callTimes.pop_front();
        if (s_callTimes.size() >= g_PBC_IdlePartyHourlyCallLimit)
            return;

        std::shuffle(view.bots.begin(), view.bots.end(), PBC_GetRNG());
        guard.groupCounter = groupCounter;
        guard.incarnation = state.incarnation;
        guard.revision = state.revision;
        guard.reservation = s_nextIdentity++;
        guard.expiresAt = now + 180;
        guard.members = state.members;
        guard.speakers.push_back(view.bots.front()->GetGUID().GetCounter());
        s_callTimes.push_back(now);

        if (view.bots.size() > 1 && PBC_RollChance(g_PBC_IdlePartyFollowupChance)
            && s_callTimes.size() < g_PBC_IdlePartyHourlyCallLimit)
        {
            guard.speakers.push_back(view.bots[1]->GetGUID().GetCounter());
            s_callTimes.push_back(now);
        }

        state.reservation = guard.reservation;
        state.reservationExpiresAt = guard.expiresAt;
        state.nextAt = now + NextDelay();
        shouldDispatch = true;
    }

    if (!shouldDispatch)
        return;

    PBC_EventItem event;
    event.type = PBC_EventType::Normal;
    event.chatType = CHAT_MSG_PARTY;
    event.eventLine = "The party has a quiet moment. Start a short, natural conversation with your companions.";
    event.canCreateEvents = false;
    event.idleGuard = guard;
    event.playerCharGuids = view.realPlayers;

    for (Player* bot : view.bots)
    {
        uint64_t guid = bot->GetGUID().GetCounter();
        if (std::find(guard.speakers.begin(), guard.speakers.end(), guid) != guard.speakers.end())
            event.respondingChars.push_back(PBC_SnapshotCharacter(bot));
        else
            event.silentCharGuids.push_back(guid);
    }

    // Preserve the selected speaker order after shuffling the eligible bots.
    std::sort(event.respondingChars.begin(), event.respondingChars.end(),
        [&guard](PBC_CharacterSnapshot const& a, PBC_CharacterSnapshot const& b)
        {
            auto ai = std::find(guard.speakers.begin(), guard.speakers.end(), a.charGuidRaw);
            auto bi = std::find(guard.speakers.begin(), guard.speakers.end(), b.charGuidRaw);
            return ai < bi;
        });

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "Idle party: admitted group={} speakers={}",
        groupCounter, guard.speakers.size());
    PBC_PushEvent(std::move(event));
}

void PBC_IdleFinishPoll(std::unordered_set<uint32_t> const& seenGroups)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!g_PBC_Enable || !g_PBC_IdlePartyEnabled)
    {
        s_states.clear();
        return;
    }
    for (auto it = s_states.begin(); it != s_states.end(); )
    {
        if (!seenGroups.count(it->first))
            it = s_states.erase(it);
        else
            ++it;
    }
}

bool PBC_IdleIsCurrent(PBC_IdleGuard const& guard)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return CurrentLocked(guard, std::time(nullptr));
}

void PBC_IdleStageCompletion(PBC_IdleGuard guard, std::vector<PBC_HistoryEntry> replies)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_completions.push({std::move(guard), std::move(replies)});
}

void PBC_IdleDrainCompletions()
{
    std::queue<Completion> completed;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        std::swap(completed, s_completions);
    }

    while (!completed.empty())
    {
        Completion const& result = completed.front();
        bool current = PBC_IdleIsCurrent(result.guard);
        Player* anchor = result.guard.members.empty() ? nullptr
            : ObjectAccessor::FindPlayer(ObjectGuid(result.guard.members.front()));
        Group* group = anchor ? anchor->GetGroup() : nullptr;
        PartyView view = InspectParty(group);
        bool accepted = current && group && view.eligible
            && group->GetGUID().GetCounter() == result.guard.groupCounter
            && view.members == result.guard.members
            && result.replies.size() <= result.guard.speakers.size();

        if (accepted)
        {
            for (size_t i = 0; i < result.replies.size(); ++i)
            {
                PBC_HistoryEntry const& reply = result.replies[i];
                if (reply.authorGuid != result.guard.speakers[i] || reply.type != CHAT_MSG_PARTY
                    || reply.message.empty() || reply.message.size() > 230)
                {
                    accepted = false;
                    break;
                }
            }
        }

        if (accepted)
        {
            for (PBC_HistoryEntry const& reply : result.replies)
            {
                Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(reply.authorGuid));
                if (!bot || bot->GetGroup() != group)
                    break;
                uint64_t id = PBC_AppendHistoryMessage(reply.authorGuid, reply.type,
                    reply.message, result.guard.members);
                if (id == 0)
                    break;
                PBC_SendPartyMessage(bot, group, reply.message);
            }
        }
        else if (!result.replies.empty())
        {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "Idle party: discarded stale reply for group={}",
                result.guard.groupCounter);
        }

        {
            std::lock_guard<std::mutex> lock(s_mutex);
            auto it = s_states.find(result.guard.groupCounter);
            if (it != s_states.end() && it->second.reservation == result.guard.reservation)
            {
                it->second.reservation = 0;
                it->second.reservationExpiresAt = 0;
                it->second.lastActivity = std::time(nullptr);
                ++it->second.revision;
            }
        }
        completed.pop();
    }
}
