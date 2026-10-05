/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: reuse native targeting, pull and pet rules with an immutable target.
#include "PlayerbotDialogueCombat.h"

#include "AttackAction.h"
#include "CharmInfo.h"
#include "ChooseTargetActions.h"
#include "Creature.h"
#include "Event.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "PetsAction.h"
#include "PlayerbotDialogue.h"
#include "PlayerbotDialogueSupport.h"
#include "Playerbots.h"
#include "PullActions.h"
#include "PullStrategy.h"

namespace PlayerbotDialogue
{
namespace
{
class BoundAttack final : public AttackAction
{
public:
    explicit BoundAttack(PlayerbotAI* ai) : AttackAction(ai, "dialogue attack") {}
    bool ExecuteOn(Unit* target) { return Attack(target); }
};

class BoundPull final : public PullRequestAction
{
public:
    BoundPull(PlayerbotAI* ai, Unit* target) : PullRequestAction(ai, "dialogue pull"), _target(target) {}

private:
    Unit* GetPullTarget(Event) override { return _target; }
    Unit* _target;
};

class BoundPet final : public PetsAction
{
public:
    BoundPet(PlayerbotAI* ai, Unit* target) : PetsAction(ai), _target(target) {}
    Unit* GetTarget() override { return _target; }

private:
    Unit* _target;
};

bool HostileTarget(Player* bot, Unit* target, Request const& request)
{
    return PlayerbotDialogueBridge::Matches(target, request.target) && target->IsAlive() && bot->IsInMap(target) &&
           bot->InSamePhase(target) && bot->CanSeeOrDetect(target) && bot->IsWithinDistInMap(target, 60.0f) &&
           bot->IsValidAttackTarget(target);
}

bool FriendlyFight(Player* bot, Unit* target, Request const& request)
{
    if (request.kind != Kind::Assist && request.kind != Kind::Protect)
        return true;
    auto subject = ObjectAccessor::GetUnit(*bot, ObjectGuid(request.subject.guid));
    return PlayerbotDialogueBridge::Matches(subject, request.subject) && subject->IsAlive() && bot->GetGroup() &&
           bot->GetGroup()->IsMember(subject->GetGUID()) && bot->IsFriendlyTo(subject) &&
           (request.kind == Kind::Assist ? subject->GetVictim() == target : target->GetVictim() == subject);
}
}  // namespace

Outcome StartCombat(Player* bot, PlayerbotAI* ai, CombatExecution& execution, uint64_t nowMs)
{
    auto const& request = execution.request;
    if (request.kind >= Kind::Buff && request.kind <= Kind::Resurrect)
        return StartSupport(bot, ai, execution, nowMs);
    auto result = [&](Status status, std::string detail)
    { return Outcome{request.id, status, std::move(detail), nowMs}; };
    auto target = request.target.guid ? ObjectAccessor::GetUnit(*bot, ObjectGuid(request.target.guid)) : nullptr;
    if (request.kind == Kind::StopAttack)
    {
        if (auto pull = PullStrategy::Get(ai))
            pull->OnPullEnded();
        ai->ChangeStrategy("+passive", BOT_STATE_NON_COMBAT);
        ai->ChangeStrategy("+passive", BOT_STATE_COMBAT);
        bot->InterruptNonMeleeSpells(false);
        DropTargetAction(ai).Execute(Event("dialogue"));
        ai->PetFollow();
        return result(!bot->GetVictim() ? Status::Completed : Status::Failed, "native_attack_stopped");
    }
    if (request.kind == Kind::Attack || request.kind == Kind::Assist || request.kind == Kind::Protect ||
        request.kind == Kind::Pull || request.kind == Kind::PetAttack)
    {
        if (!HostileTarget(bot, target, request) || !FriendlyFight(bot, target, request))
            return result(Status::Failed, "bound_hostile_target_unavailable");
        if (request.kind == Kind::Attack || request.kind == Kind::Assist || request.kind == Kind::Protect)
        {
            if (bot->GetVictim() == target)
                return result(Status::Completed, "already_attacking_bound_target");
            if (!BoundAttack(ai).ExecuteOn(target))
                return result(Status::Failed, "native_attack_refused");
            return result(bot->GetVictim() == target ? Status::Completed : Status::Started,
                          "native_attack_target_established");
        }
        if (request.kind == Kind::Pull)
        {
            if (target->IsInCombat())
                return result(Status::Failed, "target_already_in_combat");
            if (!BoundPull(ai, target).Execute(Event("dialogue")))
                return result(Status::Failed, "native_pull_unavailable");
            if (request.returnToParty)
            {
                execution.priorPullBack = ai->HasStrategy("pull back", BOT_STATE_COMBAT);
                execution.returnX = bot->GetPositionX();
                execution.returnY = bot->GetPositionY();
                execution.returnZ = bot->GetPositionZ();
                ai->ChangeStrategy("+pull back", BOT_STATE_COMBAT);
            }
            return result(Status::Started, "native_pull_requested");
        }
    }
    if (request.kind >= Kind::PetAttack && request.kind <= Kind::PetAggressive)
    {
        constexpr char const* commands[] = {"attack", "follow", "stay", "passive", "defensive", "aggressive"};
        auto pets = PetsAction::ControlledPets(bot);
        if (pets.empty())
            return result(Status::Failed, "no_native_pet");
        auto command = commands[static_cast<unsigned>(request.kind) - static_cast<unsigned>(Kind::PetAttack)];
        if (!BoundPet(ai, target).Execute(Event("dialogue", command)))
            return result(Status::Failed, "native_pet_command_refused");
        bool observed = true;
        unsigned living = 0;
        for (auto pet : pets)
        {
            auto charm = pet->GetCharmInfo();
            if (!charm || !pet->IsAlive())
                continue;
            ++living;
            switch (request.kind)
            {
                case Kind::PetAttack:
                    observed &= pet->GetVictim() == target;
                    break;
                case Kind::PetFollow:
                    observed &= !pet->GetVictim() && charm->GetCommandState() == COMMAND_FOLLOW;
                    break;
                case Kind::PetStay:
                    observed &= charm->GetCommandState() == COMMAND_STAY;
                    break;
                case Kind::PetPassive:
                    observed &= pet->GetReactState() == REACT_PASSIVE;
                    break;
                case Kind::PetDefensive:
                    observed &= pet->GetReactState() == REACT_DEFENSIVE;
                    break;
                case Kind::PetAggressive:
                    observed &= pet->GetReactState() == REACT_AGGRESSIVE;
                    break;
                default:
                    break;
            }
        }
        return result(observed && living ? Status::Completed : Status::Failed, "native_pet_state_observed");
    }
    return result(Status::Failed, "unsupported_combat_operation");
}

Outcome PollCombat(Player* bot, PlayerbotAI* ai, CombatExecution& execution, uint64_t nowMs)
{
    auto const& request = execution.request;
    if (request.kind >= Kind::Buff && request.kind <= Kind::Resurrect)
        return PollSupport(bot, ai, execution, nowMs);
    auto target = ObjectAccessor::GetUnit(*bot, ObjectGuid(request.target.guid));
    auto result = [&](Status status, std::string detail)
    { return Outcome{request.id, status, std::move(detail), nowMs}; };
    if (!HostileTarget(bot, target, request) || !FriendlyFight(bot, target, request))
        return result(Status::Failed, "bound_hostile_target_lost");
    // A ranged pull can establish combat before melee auto-attack owns a victim.
    // The bound native combat reference is the actual engagement outcome.
    if (request.kind == Kind::Pull ? bot->IsInCombatWith(target) : bot->GetVictim() == target)
    {
        if (!request.returnToParty)
            return result(Status::Completed, "native_engagement_observed");
        if (bot->GetExactDist(execution.returnX, execution.returnY, execution.returnZ) <= 3.0f &&
            target->GetExactDist(execution.returnX, execution.returnY, execution.returnZ) <= 10.0f &&
            bot->IsWithinLOSInMap(target))
            return result(Status::Completed, "native_enemy_brought_to_prepared_party");
    }
    if (nowMs >= request.deadlineMs || nowMs >= execution.startedMs + (request.returnToParty ? 90000 : 20000))
        return result(Status::Failed, "native_engagement_not_observed");
    return result(Status::Pending, "awaiting_native_engagement");
}

void CancelCombat(PlayerbotAI* ai, Request const& request)
{
    if (request.kind >= Kind::Buff && request.kind <= Kind::Resurrect)
        CancelSupport(ai, request);
    if (request.kind == Kind::Pull)
        if (auto pull = PullStrategy::Get(ai))
            pull->OnPullEnded();
}

void FinishPartyPull(PlayerbotAI* ai, CombatExecution const& execution)
{
    if (execution.request.returnToParty && !execution.priorPullBack)
        ai->ChangeStrategy("-pull back", BOT_STATE_COMBAT);
}
}  // namespace PlayerbotDialogue
