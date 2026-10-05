/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: fixed native duel spell, bound opponent and conservative interruption.
#include "PlayerbotDialogueDuel.h"

#include <map>
#include <mutex>

#include "DBCStores.h"
#include "ObjectAccessor.h"
#include "PlayerbotDialogue.h"
#include "Playerbots.h"
#include "SocialMgr.h"

namespace PlayerbotDialogue
{
namespace
{
std::mutex CooldownMutex;
std::map<std::pair<uint64_t, uint64_t>, uint64_t> NextChallenge;
bool BoundDuel(Player* bot, Player* human, DuelExecution const& execution)
{
    return human && bot->duel && human->duel && bot->duel->Opponent == human && human->duel->Opponent == bot &&
           bot->duel->Initiator == bot && human->duel->Initiator == bot &&
           (!execution.arbiter || bot->GetGuidValue(PLAYER_DUEL_ARBITER).GetRawValue() == execution.arbiter) &&
           bot->GetGuidValue(PLAYER_DUEL_ARBITER) == human->GetGuidValue(PLAYER_DUEL_ARBITER);
}
}  // namespace

bool CanOfferDuel(Player* bot, Player* human, uint64_t nowMs)
{
    if (!bot || !human || bot == human || !bot->HasSpell(7266) || !human->GetSession() ||
        human->GetSession()->IsBot() || !bot->IsAlive() || !human->IsAlive() || bot->IsInCombat() ||
        human->IsInCombat() || bot->duel || human->duel || !bot->IsInMap(human) || !bot->InSamePhase(human) ||
        !bot->IsWithinDistInMap(human, 25.0f) || !bot->IsWithinLOSInMap(human) || bot->GetHealthPct() < 90.0f ||
        human->GetHealthPct() < 90.0f || bot->GetLevel() < 3 || human->GetLevel() < 3 ||
        human->GetLevel() > bot->GetLevel() + 3 || bot->GetLevel() > human->GetLevel() + 10 || !human->GetSocial() ||
        human->GetSocial()->HasIgnore(bot->GetGUID()))
        return false;
    for (auto player : {bot, human})
        if (auto area = sAreaTableStore.LookupEntry(player->GetAreaId());
            area && !(area->flags & AREA_FLAG_ALLOW_DUELS))
            return false;
    std::lock_guard lock(CooldownMutex);
    auto found = NextChallenge.find({bot->GetGUID().GetRawValue(), human->GetGUID().GetRawValue()});
    return found == NextChallenge.end() || nowMs >= found->second;
}

Outcome StartDuel(Player* bot, PlayerbotAI* ai, DuelExecution& execution, uint64_t nowMs)
{
    auto const& request = execution.request;
    auto human = ObjectAccessor::FindPlayer(ObjectGuid(request.target.guid));
    if (!PlayerbotDialogueBridge::Matches(human, request.target) || request.target.guid != request.requester.guid ||
        !CanOfferDuel(bot, human, nowMs) || !ai->CanCastSpell(7266, human, true))
        return {request.id, Status::Failed, "native_duel_unavailable_or_cooldown", nowMs};
    {
        std::lock_guard lock(CooldownMutex);
        std::erase_if(NextChallenge, [&](auto const& entry) { return nowMs >= entry.second; });
        NextChallenge[{request.actor.guid, request.target.guid}] = nowMs + 600000;
    }
    if (!ai->CastSpell(7266, human))
        return {request.id, Status::Failed, "native_duel_cast_refused", nowMs};
    if (BoundDuel(bot, human, execution))
        execution.arbiter = bot->GetGuidValue(PLAYER_DUEL_ARBITER).GetRawValue();
    return {request.id, Status::Started, "native_duel_challenge_pending_human_consent", nowMs};
}

Outcome PollDuel(Player* bot, DuelExecution& execution, uint64_t nowMs)
{
    auto const& request = execution.request;
    auto human = ObjectAccessor::FindPlayer(ObjectGuid(request.target.guid));
    if (!PlayerbotDialogueBridge::Matches(human, request.target))
        return {request.id, Status::Failed, "native_duel_opponent_unavailable", nowMs};
    if (!BoundDuel(bot, human, execution))
        return {request.id, execution.arbiter || nowMs > execution.startedMs + 3000 ? Status::Failed : Status::Pending,
                "native_duel_not_accepted_or_interrupted", nowMs};
    execution.arbiter = bot->GetGuidValue(PLAYER_DUEL_ARBITER).GetRawValue();
    if (bot->duel->State == DUEL_STATE_COUNTDOWN || bot->duel->State == DUEL_STATE_IN_PROGRESS)
        return {request.id, Status::Completed, "native_duel_accepted_not_won", nowMs};
    if (nowMs >= request.deadlineMs || nowMs >= execution.startedMs + 60000)
        return {request.id, Status::Failed, "native_duel_offer_expired", nowMs};
    return {request.id, Status::Pending, "native_duel_waiting_for_human", nowMs};
}

void CancelDuel(Player* bot, DuelExecution const& execution)
{
    auto human = ObjectAccessor::FindPlayer(ObjectGuid(execution.request.target.guid));
    // Never invoke the generic cancel opcode after acceptance: that can turn a
    // stale dialogue cancellation into a forfeit of a different/active duel.
    if (execution.arbiter && BoundDuel(bot, human, execution) && bot->duel->State == DUEL_STATE_CHALLENGED &&
        human->duel->State == DUEL_STATE_CHALLENGED)
        bot->DuelComplete(DUEL_INTERRUPTED);
}
}  // namespace PlayerbotDialogue
