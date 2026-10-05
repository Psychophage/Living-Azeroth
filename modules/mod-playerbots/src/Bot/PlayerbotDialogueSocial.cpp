/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: called on the world thread after the bridge revalidates authority.
#include "PlayerbotDialogueSocial.h"

#include "Group.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotDialogue.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "QuestPackets.h"
#include "WorldPacket.h"
#include "WorldSession.h"

namespace PlayerbotDialogue
{
namespace
{
bool CanOffer(Quest const* quest)
{
    // A dialogue share is an offer. Custom autoaccept quests would grant progress
    // to recipients before their consent; leave those to the ordinary quest UI.
    return quest && !quest->IsAutoAccept() && !quest->IsAutoComplete() && quest->GetQuestMethod();
}
}  // namespace

std::vector<QuestOffer> QuestOffers(Player* bot, Unit* provider)
{
    std::vector<QuestOffer> result;
    if (!bot || !provider || !bot->IsInMap(provider) || !bot->InSamePhase(provider))
        return result;
    auto add = [&](uint32_t id, Kind kind)
    {
        auto quest = sObjectMgr->GetQuestTemplate(id);
        if (quest && (kind == Kind::ShareQuest ? CanOffer(quest)
                                               : bot->CanTakeQuest(quest, false) && bot->CanAddQuest(quest, false)))
            result.push_back({kind, id, PlayerbotDialogueBridge::Snapshot(provider), quest->GetTitle()});
    };
    if (provider == bot)
    {
        for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            if (auto id = bot->GetQuestSlotQuestId(slot); id && bot->CanShareQuest(id))
                add(id, Kind::ShareQuest);
    }
    else if (auto player = provider->ToPlayer())
    {
        if (player->GetGroup() && player->GetGroup() == bot->GetGroup())
            for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
                if (auto id = player->GetQuestSlotQuestId(slot); id)
                {
                    if (player->CanShareQuest(id))
                        add(id, Kind::AcceptQuest);
                    if (player->GetQuestStatus(id) == QUEST_STATUS_INCOMPLETE)
                        if (auto quest = sObjectMgr->GetQuestTemplate(id))
                            result.push_back(
                                {Kind::QuestHelp, id, PlayerbotDialogueBridge::Snapshot(provider), quest->GetTitle()});
                }
    }
    else if (provider->IsCreature())
    {
        auto [begin, end] = sObjectMgr->GetCreatureQuestRelationBounds(provider->GetEntry());
        for (auto it = begin; it != end; ++it)
            add(it->second, Kind::AcceptQuest);
    }
    return result;
}

Outcome RunSocial(Player* bot, Request const& request, uint64_t nowMs)
{
    auto fail = [&](std::string detail) { return Outcome{request.id, Status::Failed, std::move(detail), nowMs}; };
    auto done = [&](std::string detail) { return Outcome{request.id, Status::Completed, std::move(detail), nowMs}; };
    auto requester = ObjectAccessor::FindPlayer(ObjectGuid(request.requester.guid));
    if (!requester || !bot->GetSession())
        return fail("native_social_requester_unavailable");
    // A normal invitation can be accepted between capture and dispatch. Join's
    // dedicated check below permits only the requested group, without moving a
    // character out of a different group or accepting a different invitation.
    if (request.kind != Kind::Join &&
        (bot->GetGroup() ? bot->GetGroup()->GetGUID().GetRawValue() : 0) != request.actorGroup)
        return fail("native_group_changed");
    if (request.kind == Kind::Leave)
    {
        if (!bot->GetGroup())
            return done("already_outside_group");
        WorldPacket packet(CMSG_GROUP_DISBAND);
        bot->GetSession()->HandleGroupDisbandOpcode(packet);
        // Group hooks may change ownership. Resolve again after the handler.
        bot = ObjectAccessor::FindPlayer(ObjectGuid(request.actor.guid));
        return bot && !bot->GetGroup() ? done("native_group_left") : fail("native_group_leave_refused");
    }
    auto target = ObjectAccessor::GetUnit(*bot, ObjectGuid(request.target.guid));
    if (!PlayerbotDialogueBridge::Matches(target, request.target) || !bot->InSamePhase(target) ||
        !bot->CanSeeOrDetect(target) || !bot->IsWithinDistInMap(target, 60.0f))
        return fail("native_social_target_changed");
    if (request.kind == Kind::QuestHelp)
    {
        if (target != requester || !bot->GetGroup() || bot->GetGroup() != requester->GetGroup() ||
            requester->GetQuestStatus(request.objectId) != QUEST_STATUS_INCOMPLETE)
            return fail("native_current_objective_unavailable");
        auto ai = GET_PLAYERBOT_AI(bot);
        if (!ai)
            return fail("native_quest_companion_unavailable");
        // Use the companion's normal target-assist and loot machinery. This
        // establishes help while adventuring with the human, never remote quest
        // completion, reward selection or a new autonomous travel destination.
        bool tank = ai->IsTank(bot);
        ai->ChangeStrategy(tank ? "+tank assist" : "+dps assist", BOT_STATE_COMBAT);
        ai->ChangeStrategy("+loot", BOT_STATE_NON_COMBAT);
        return ai->HasStrategy(tank ? "tank assist" : "dps assist", BOT_STATE_COMBAT) &&
                       ai->HasStrategy("loot", BOT_STATE_NON_COMBAT)
                   ? done("native_objective_assistance_enabled_not_completed")
                   : fail("native_objective_assistance_unavailable");
    }
    if (request.kind == Kind::ShareQuest)
    {
        auto quest = sObjectMgr->GetQuestTemplate(request.objectId);
        if (target != requester || !bot->GetGroup() || bot->GetGroup() != requester->GetGroup() ||
            !bot->CanShareQuest(request.objectId) || !CanOffer(quest))
            return fail("native_quest_cannot_share");
        if (requester->GetDivider() || !requester->CanTakeQuest(quest, false) || !requester->CanAddQuest(quest, false))
            return fail("native_quest_recipient_ineligible");
        WorldPacket packet(CMSG_PUSHQUESTTOPARTY);
        packet << request.objectId;
        WorldPackets::Quest::PushQuestToParty push(std::move(packet));
        push.Read();
        bot->GetSession()->HandlePushQuestToParty(push);
        return requester->GetDivider() == bot->GetGUID() ? done("native_quest_offered_to_party_not_accepted")
                                                         : fail("native_quest_share_refused");
    }
    if (request.kind == Kind::AcceptQuest)
    {
        bool offered = false;
        for (auto const& offer : QuestOffers(bot, target))
            offered |= offer.kind == Kind::AcceptQuest && offer.id == request.objectId;
        if (!offered || !bot->CanInteractWithQuestGiver(target))
            return fail("native_quest_unavailable_or_out_of_range");
        WorldPacket packet(CMSG_QUESTGIVER_ACCEPT_QUEST);
        packet << target->GetGUID() << request.objectId << uint32(0);
        bot->GetSession()->HandleQuestgiverAcceptQuestOpcode(packet);
        return bot->GetQuestStatus(request.objectId) == QUEST_STATUS_INCOMPLETE ||
                       bot->GetQuestStatus(request.objectId) == QUEST_STATUS_COMPLETE
                   ? done("native_quest_accepted_not_rewarded")
                   : fail("native_quest_accept_refused");
    }
    auto player = target->ToPlayer();
    if (!player)
        return fail("native_group_requires_player");
    if (request.kind == Kind::Invite)
    {
        if (player == bot || player->GetGroup() || player->GetGroupInvite())
            return fail("native_group_target_unavailable");
        WorldPacket packet(CMSG_GROUP_INVITE);
        packet << player->GetName() << uint32(0);
        bot->GetSession()->HandleGroupInviteOpcode(packet);
        auto invitation = player->GetGroupInvite();
        auto ownGroup = bot->GetGroup() ? bot->GetGroup() : bot->GetGroupInvite();
        return invitation && invitation == ownGroup ? done("native_group_invitation_sent_not_joined")
                                                    : fail("native_group_invite_refused");
    }
    if (request.kind == Kind::Join)
    {
        if (bot->GetGroup())
            return bot->GetGroup() == player->GetGroup() ? done("already_in_requested_group")
                                                         : fail("native_group_already_member_elsewhere");
        auto invitation = bot->GetGroupInvite();
        if (!invitation || invitation->GetLeaderGUID().GetRawValue() != request.invitingLeader ||
            (invitation != player->GetGroup() && invitation != player->GetGroupInvite()))
            return fail("native_group_invitation_changed_or_absent");
        auto leader = invitation->GetLeaderGUID();
        WorldPacket packet(CMSG_GROUP_ACCEPT);
        packet << uint32(0);
        bot->GetSession()->HandleGroupAcceptOpcode(packet);
        bot = ObjectAccessor::FindPlayer(ObjectGuid(request.actor.guid));
        return bot && bot->GetGroup() && bot->GetGroup()->IsMember(leader) ? done("native_group_joined")
                                                                           : fail("native_group_join_refused");
    }
    return fail("unsupported_native_social_action");
}
}  // namespace PlayerbotDialogue
