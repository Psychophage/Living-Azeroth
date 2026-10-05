/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: native execution, with no networking or database calls in a map tick.
#include "PlayerbotDialogue.h"
#include "PlayerbotFactory.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>

#include "ChatShortcutActions.h"
#include "Event.h"
#include "Formations.h"
#include "ObjectAccessor.h"
#include "PlayerbotDialogueCombat.h"
#include "PlayerbotDialogueDuel.h"
#include "PlayerbotDialogueInventory.h"
#include "PlayerbotDialoguePerformance.h"
#include "PlayerbotDialogueProgression.h"
#include "PlayerbotDialogueServices.h"
#include "PlayerbotDialogueSocial.h"
#include "PlayerbotDialogueSupport.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "Playerbots.h"
#include "PositionValue.h"
#include "ScriptMgr.h"

namespace
{
using namespace PlayerbotDialogue;

struct MovementState
{
    std::string nonCombat;
    std::string combat;
    std::string formation;
    PositionInfo stay;
    PositionInfo returned;
};

struct Mailbox
{
    std::mutex mutex;
    uint64_t revision = 1;
    uint64_t latestSequence = 0;
    std::optional<Request> pending;
    std::optional<Request> moving;
    std::optional<CombatExecution> combat;
    std::vector<CombatExecution> cancelCombat;
    std::optional<InventoryExecution> inventory;
    PerformanceState performance;
    std::optional<Request> service;
    std::optional<Request> social;
    std::optional<Request> supplyOffer;
    std::optional<DuelExecution> duel;
    std::vector<DuelExecution> cancelDuels;
    std::optional<SpecExecution> specialization;
    std::vector<SpecExecution> cancelSpecs;
    bool stopServiceMovement = false;
    std::vector<Request> cancelInventory;
    std::optional<std::pair<bool, bool>> priorPassive;
    std::optional<MovementState> priorMovement;
    std::optional<Request> hold;
    uint64_t releaseAt = 0;
    bool restore = false;
    bool preparedReady = false;
    std::string completedPull;
    std::vector<Outcome> outcomes;
};

std::atomic_bool Enabled{false};
std::atomic_bool HasMailboxes{false};
std::atomic<uint64_t> Now{0};
std::mutex MailboxesMutex;
std::map<uint64_t, std::shared_ptr<Mailbox>> Mailboxes;
std::mutex LivesMutex;
std::map<std::tuple<uint64_t, uint32_t, uint32_t>, uint64_t> Lives;
uint64_t NextLife = 0;

class DialogueCreatureLife final : public AllCreatureScript
{
public:
    DialogueCreatureLife() : AllCreatureScript("LivingAzeroth_PlayerbotDialogueLife") {}
    void OnCreatureSelectLevel(CreatureTemplate const*, Creature* creature) override
    {
        PlayerbotDialogueBridge::InvalidateLife(creature);
    }
    void OnCreatureRemoveWorld(Creature* creature) override { PlayerbotDialogueBridge::InvalidateLife(creature); }
};

class DialogueUnitLife final : public UnitScript
{
public:
    DialogueUnitLife() : UnitScript("LivingAzeroth_PlayerbotDialogueDeath", true, {UNITHOOK_ON_UNIT_DEATH}) {}
    void OnUnitDeath(Unit* unit, Unit*) override { PlayerbotDialogueBridge::InvalidateLife(unit); }
};

class DialogueResurrection final : public PlayerScript
{
public:
    DialogueResurrection() : PlayerScript("LivingAzeroth_PlayerbotDialogueResurrection", {PLAYERHOOK_ON_PLAYER_RESURRECT}) {}
    void OnPlayerResurrect(Player* player, float restoredFraction, bool&) override
    {
        ObserveResurrection(player, restoredFraction);
    }
};

// The registry lock is released before taking a mailbox lock. Each native action
// holds only its own bot's mailbox, never locks another bot or calls a database.
std::shared_ptr<Mailbox> FindMailbox(uint64_t bot, bool create = false)
{
    std::lock_guard lock(MailboxesMutex);
    auto found = Mailboxes.find(bot);
    if (found != Mailboxes.end())
        return found->second;
    if (!create)
        return {};
    auto mailbox = std::make_shared<Mailbox>();
    Mailboxes.emplace(bot, mailbox);
    HasMailboxes.store(true);
    return mailbox;
}

void Record(Mailbox& mailbox, Request const& request, Status status, std::string detail)
{
    mailbox.outcomes.push_back({request.id, status, std::move(detail), Now.load()});
}

void RestorePassive(PlayerbotAI* ai, Mailbox& mailbox)
{
    if (!mailbox.priorPassive)
        return;
    ai->ChangeStrategy(mailbox.priorPassive->first ? "+passive" : "-passive", BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy(mailbox.priorPassive->second ? "+passive" : "-passive", BOT_STATE_COMBAT);
    mailbox.priorPassive.reset();
}

void QueueCombatCancellation(Mailbox& mailbox, std::string const& reason)
{
    mailbox.supplyOffer.reset();
    if (mailbox.duel)
    {
        Record(mailbox, mailbox.duel->request, Status::Cancelled, reason);
        mailbox.cancelDuels.push_back(*mailbox.duel);
        mailbox.duel.reset();
    }
    if (mailbox.social)
    {
        Record(mailbox, *mailbox.social, Status::Cancelled, reason);
        mailbox.social.reset();
    }
    if (mailbox.specialization)
    {
        Record(mailbox, mailbox.specialization->request, Status::Cancelled, reason);
        mailbox.cancelSpecs.push_back(*mailbox.specialization);
        mailbox.specialization.reset();
    }
    if (mailbox.service)
    {
        // A service waiting to approach/cast has spent nothing. Native money
        // mutations finish synchronously on this owning map thread.
        Record(mailbox, *mailbox.service, Status::Cancelled, reason);
        mailbox.service.reset();
        mailbox.stopServiceMovement = true;
    }
    if (mailbox.inventory)
    {
        Record(mailbox, mailbox.inventory->request, Status::Unresolved, reason);
        mailbox.cancelInventory.push_back(mailbox.inventory->request);
        mailbox.inventory.reset();
    }
    if (mailbox.combat)
    {
        Record(mailbox, mailbox.combat->request, Status::Cancelled, reason);
        mailbox.cancelCombat.push_back(*mailbox.combat);
        mailbox.combat.reset();
    }
}

void FinishInventoryCancellations(Player* bot, Mailbox& mailbox)
{
    for (auto const& request : mailbox.cancelInventory)
    {
        auto outcome = PollInventory(bot, InventoryExecution{request, Now.load()}, Now.load());
        CancelInventory(bot, request);
        if (outcome.status == Status::Pending || outcome.status == Status::Started)
            outcome = {request.id, Status::Cancelled, "native_inventory_cancelled_before_settlement", Now.load()};
        mailbox.outcomes.push_back(std::move(outcome));
    }
    mailbox.cancelInventory.clear();
}

MovementState CaptureMovement(PlayerbotAI* ai)
{
    MovementState state;
    state.formation = ai->GetAiObjectContext()->GetValue<Formation*>("formation")->Get()->getName();
    for (auto name : {"follow", "stay", "guard", "runaway", "passive", "grind", "move from group"})
    {
        state.nonCombat += std::string(ai->HasStrategy(name, BOT_STATE_NON_COMBAT) ? "+" : "-") + name + ",";
        state.combat += std::string(ai->HasStrategy(name, BOT_STATE_COMBAT) ? "+" : "-") + name + ",";
    }
    state.nonCombat.pop_back();
    state.combat.pop_back();
    auto& positions = ai->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get();
    state.stay = positions["stay"];
    state.returned = positions["return"];
    return state;
}

void RefreshCombat(PlayerbotAI* ai)
{
    auto movement = CaptureMovement(ai);
    ai->SelectiveResetStrategies(BOT_STATE_COMBAT);
    ai->ChangeStrategy(movement.combat, BOT_STATE_COMBAT);
    // Rebuild rotations while preserving standing movement orders/timers.
    auto& positions = ai->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get();
    positions["stay"] = movement.stay;
    positions["return"] = movement.returned;
}

void RestoreMovement(PlayerbotAI* ai, Mailbox& mailbox)
{
    bool preparedHold = mailbox.hold && mailbox.hold->kind == Kind::Formation && mailbox.hold->objectId == 9;
    mailbox.preparedReady = false;
    if (mailbox.priorMovement)
    {
        static_cast<FormationValue*>(ai->GetAiObjectContext()->GetValue<Formation*>("formation"))
            ->Load(mailbox.priorMovement->formation);
        ai->ChangeStrategy(mailbox.priorMovement->nonCombat, BOT_STATE_NON_COMBAT);
        ai->ChangeStrategy(mailbox.priorMovement->combat, BOT_STATE_COMBAT);
        auto& positions = ai->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get();
        positions["stay"] = mailbox.priorMovement->stay;
        positions["return"] = mailbox.priorMovement->returned;
    }
    mailbox.priorMovement.reset();
    mailbox.hold.reset();
    mailbox.releaseAt = 0;
    if (preparedHold)
        RestorePassive(ai, mailbox);
    else if (mailbox.priorPassive)
    {
        ai->ChangeStrategy("+passive", BOT_STATE_NON_COMBAT);
        ai->ChangeStrategy("+passive", BOT_STATE_COMBAT);
    }
}

bool Valid(Player* bot, PlayerbotAI* ai, Mailbox const& mailbox, Request const& request)
{
    auto requester = ObjectAccessor::FindPlayer(ObjectGuid(request.requester.guid));
    bool permitted = request.voluntary
                         ? (request.kind == Kind::Invite ? PlayerbotDialogueBridge::AllowsInvitation(bot, requester)
                                                         : PlayerbotDialogueBridge::AllowsInitiative(bot, requester))
                         : requester && ai->GetMaster() == requester &&
                               ai->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_ALLOW_ALL, true, requester);
    if (request.voluntary && request.kind != Kind::Approach && request.kind != Kind::Buff &&
        request.kind != Kind::Heal && request.kind != Kind::Cleanse && request.kind != Kind::Duel &&
        request.kind != Kind::OfferSupplies && request.kind != Kind::Invite)
        permitted = false;
    return permitted && PlayerbotDialogueBridge::Matches(bot, request.actor) && bot->IsAlive() &&
           !bot->IsBeingTeleported() && PlayerbotDialogueBridge::Matches(requester, request.requester) &&
           requester->IsInMap(bot) && requester->InSamePhase(bot) && requester->GetSession() &&
           !requester->GetSession()->IsBot() && !requester->GetSession()->IsSocketClosed() &&
           !requester->GetSession()->IsLoggingOut() && request.authorityVersion == mailbox.revision;
}

class DialogueApproach final : public MovementAction
{
public:
    explicit DialogueApproach(PlayerbotAI* ai) : MovementAction(ai, "dialogue approach") {}
    bool Approach(Unit* target) { return target && IsMovingAllowed(target) && MoveNear(target, 2.0f); }
    bool Place(WorldLocation const& location, bool forced = false)
    {
        return !Formation::IsNullLocation(location) && IsMovingAllowed() &&
               MoveTo(location.GetMapId(), location.GetPositionX(), location.GetPositionY(), location.GetPositionZ(),
                      false, false, false, false,
                      forced ? MovementPriority::MOVEMENT_FORCED : MovementPriority::MOVEMENT_NORMAL);
    }
};

class DialogueSocialOperation final : public PlayerbotOperation
{
public:
    explicit DialogueSocialOperation(Request request) : _request(std::move(request)) {}
    bool Execute() override
    {
        PlayerbotDialogueBridge::ExecuteSocial(_request);
        return true;
    }
    ObjectGuid GetBotGuid() const override { return ObjectGuid(_request.actor.guid); }
    std::string GetName() const override { return "Living Azeroth dialogue social action"; }

private:
    Request _request;
};

void Start(Player* bot, PlayerbotAI* ai, Mailbox& mailbox, Request const& request)
{
    if (!Valid(bot, ai, mailbox, request) || Now.load() >= request.deadlineMs)
    {
        Record(mailbox, request, Status::Failed, "authority_or_world_changed");
        return;
    }
    if (mailbox.performance.danceEndsMs && request.kind != Kind::Dance)
    {
        StopPerformance(bot, mailbox.performance);
        if (mailbox.performance.ownsMovement)
            RestoreMovement(ai, mailbox);
        mailbox.performance.ownsMovement = false;
    }
    if (request.kind >= Kind::Unequip && request.kind <= Kind::StopDance)
    {
        if (request.kind == Kind::Dance && !bot->IsInCombat() && !bot->IsMounted())
        {
            if (!mailbox.hold)
            {
                if (!mailbox.priorMovement)
                    mailbox.priorMovement = CaptureMovement(ai);
                mailbox.performance.ownsMovement = true;
            }
            StayChatShortcutAction(ai, true).Execute(Event("dialogue"));
        }
        auto outcome = Perform(bot, request, mailbox.performance, Now.load());
        if (outcome.status == Status::Failed && mailbox.performance.ownsMovement)
        {
            RestoreMovement(ai, mailbox);
            mailbox.performance.ownsMovement = false;
        }
        mailbox.outcomes.push_back(std::move(outcome));
        return;
    }
    if (request.kind == Kind::Approach && request.target.guid == request.actor.guid)
    {
        Record(mailbox, request, Status::Failed, "cannot_approach_self");
        return;
    }
    if (request.voluntary && request.kind == Kind::Approach &&
        (mailbox.hold || ai->HasStrategy("stay", BOT_STATE_NON_COMBAT) ||
         ai->HasStrategy("guard", BOT_STATE_NON_COMBAT)))
    {
        Record(mailbox, request, Status::Failed, "standing_order_prevents_personal_movement");
        return;
    }
    if (request.kind == Kind::OfferSupplies)
    {
        bool available = false;
        auto human = ObjectAccessor::FindPlayer(ObjectGuid(request.requester.guid));
        if (request.voluntary && ai->GetMaster() == human && request.items.size() == 1 && request.quantity &&
            request.quantity <= 3 && (!mailbox.supplyOffer || Now.load() >= mailbox.supplyOffer->deadlineMs))
            for (auto const& item : SupplyOptions(ai))
                if (item.guid == request.items.front().guid && item.entry == request.objectId &&
                    item.count == request.items.front().count && item.count > request.quantity)
                    available = true;
        if (available)
            mailbox.supplyOffer = request;
        Record(mailbox, request, available ? Status::Completed : Status::Failed,
               available ? "supplies_offered_not_transferred" : "supplies_unavailable_or_offer_pending");
        return;
    }
    if (request.kind == Kind::DeclineOffer || !request.offerId.empty())
    {
        auto const& offer = mailbox.supplyOffer;
        bool matches = offer && offer->id == request.offerId && offer->requester.guid == request.requester.guid &&
                       offer->authorityVersion == request.authorityVersion && Now.load() < offer->deadlineMs &&
                       (request.kind == Kind::DeclineOffer ||
                        (request.kind == Kind::Trade && request.objectId == offer->objectId &&
                         request.quantity == offer->quantity && request.items.size() == 1 &&
                         request.items.front().guid == offer->items.front().guid));
        if (!matches)
        {
            Record(mailbox, request, Status::Failed, "supply_offer_expired_or_changed");
            return;
        }
        mailbox.supplyOffer.reset();  // One answer consumes it; uncertain trade is never replayed.
        if (request.kind == Kind::DeclineOffer)
        {
            Record(mailbox, request, Status::Completed, "supply_offer_declined_no_transfer");
            return;
        }
    }
    if (request.kind >= Kind::Attack && request.kind <= Kind::PetAggressive)
    {
        if (mailbox.hold && mailbox.hold->kind == Kind::Formation && mailbox.hold->objectId == 9 &&
            (request.kind == Kind::Attack || request.kind == Kind::Pull || request.kind == Kind::Assist ||
             request.kind == Kind::Protect))
            RestoreMovement(ai, mailbox);
        if (mailbox.combat)
        {
            FinishPartyPull(ai, *mailbox.combat);
            CancelCombat(ai, mailbox.combat->request);
            Record(mailbox, mailbox.combat->request, Status::Cancelled, "superseded");
            mailbox.combat.reset();
        }
        if (request.kind == Kind::StopAttack && !mailbox.priorPassive)
            mailbox.priorPassive = {ai->HasStrategy("passive", BOT_STATE_NON_COMBAT),
                                    ai->HasStrategy("passive", BOT_STATE_COMBAT)};
        CombatExecution execution{request, Now.load()};
        auto outcome = StartCombat(bot, ai, execution, Now.load());
        if ((outcome.status == Status::Started || outcome.status == Status::Completed) &&
            (request.kind == Kind::Attack || request.kind == Kind::Pull || request.kind == Kind::Assist ||
             request.kind == Kind::Protect))
        {
            // Only an accepted new combat order releases a prior stop. A failed
            // friendly/ambiguous attack must not silently resume old combat.
            RestorePassive(ai, mailbox);
            ai->ChangeStrategy("-passive", BOT_STATE_NON_COMBAT);
            ai->ChangeStrategy("-passive", BOT_STATE_COMBAT);
        }
        mailbox.outcomes.push_back(outcome);
        if (outcome.status == Status::Started)
            mailbox.combat = std::move(execution);
        else if (outcome.status == Status::Failed)
            CancelCombat(ai, request);
        return;
    }

    // A fresh explicit transfer supersedes a previous supply invitation to the
    // same human, including a changed quantity. Completed/cancelled transfers
    // must not leave an old native offer that vague assent can accept again.
    if (request.kind == Kind::Trade && mailbox.supplyOffer &&
        mailbox.supplyOffer->requester.guid == request.requester.guid)
        mailbox.supplyOffer.reset();
    if (request.kind >= Kind::Inspect && request.kind <= Kind::Trade)
    {
        InventoryExecution execution{request, Now.load()};
        auto outcome = StartInventory(bot, ai, execution, Now.load());
        mailbox.outcomes.push_back(outcome);
        if (outcome.status == Status::Started)
            mailbox.inventory = std::move(execution);
        else
            CancelInventory(bot, request);
        return;
    }
    if (request.kind == Kind::Duel)
    {
        DuelExecution execution{request, Now.load()};
        auto outcome = StartDuel(bot, ai, execution, Now.load());
        mailbox.outcomes.push_back(outcome);
        if (outcome.status == Status::Started)
            mailbox.duel = execution;
        return;
    }
    if (request.kind == Kind::Role)
    {
        mailbox.outcomes.push_back(ChangeRole(bot, ai, request, Now.load()));
        return;
    }
    if (request.kind == Kind::Spec)
    {
        SpecExecution execution{request};
        auto outcome = StartSpec(bot, execution, Now.load());
        mailbox.outcomes.push_back(outcome);
        if (outcome.status == Status::Started)
            mailbox.specialization = execution;
        return;
    }
    if ((request.kind >= Kind::Train && request.kind <= Kind::Buy) || request.kind == Kind::Talents)
    {
        auto outcome = RunService(bot, ai, request, Now.load());
        if (outcome.detail == "native_talents_rebuilt" || outcome.detail == "native_talent_build_incomplete")
            RefreshCombat(ai);
        mailbox.outcomes.push_back(outcome);
        if (outcome.status == Status::Started)
            mailbox.service = request;
        return;
    }
    if (request.kind >= Kind::ShareQuest && request.kind <= Kind::Leave)
    {
        mailbox.social = request;
        if (PlayerbotWorldThreadProcessor::instance().QueueOperation(
                std::make_unique<DialogueSocialOperation>(request)))
            Record(mailbox, request, Status::Started, "native_social_queued");
        else
        {
            mailbox.social.reset();
            Record(mailbox, request, Status::Failed, "native_world_queue_full");
        }
        return;
    }
    if (request.kind > Kind::Resume && request.kind != Kind::Cancel)
    {
        Record(mailbox, request, Status::Failed, "unsupported_native_action");
        return;
    }
    if (mailbox.moving)
    {
        Record(mailbox, *mailbox.moving, Status::Cancelled, "superseded");
        mailbox.moving.reset();
    }
    if (request.kind == Kind::Cancel || request.kind == Kind::Resume)
    {
        RestoreMovement(ai, mailbox);
        RestorePassive(ai, mailbox);
        Record(mailbox, request, Status::Completed, "previous_movement_restored");
        return;
    }
    if (mailbox.hold && mailbox.hold->kind == Kind::Formation && mailbox.hold->objectId == 9)
        RestorePassive(ai, mailbox);  // A new movement order releases only the temporary preparation restriction.
    if (request.kind == Kind::Formation)
    {
        // These IDs are protocol options, never arbitrary strategy names.
        constexpr char const* names[] = {"near",   "far",   "line",  "circle", "arrow",
                                         "shield", "queue", "chaos", "spread", "spread_hold"};
        if (request.objectId >= std::size(names))
        {
            Record(mailbox, request, Status::Failed, "invalid_formation");
            return;
        }
        auto value = static_cast<FormationValue*>(ai->GetAiObjectContext()->GetValue<Formation*>("formation"));
        auto previous = CaptureMovement(ai);
        bool loaded = value->Load(names[request.objectId]);
        if (loaded && request.objectId >= 8)
        {
            auto location = value->Get()->GetLocation();
            if (Formation::IsNullLocation(location))
            {
                value->Load(previous.formation);
                Record(mailbox, request, Status::Failed, "no_reachable_spaced_position");
                return;
            }
            if (!mailbox.priorMovement)
                mailbox.priorMovement = std::move(previous);
            mailbox.releaseAt = 0;
            mailbox.hold.reset();
            bool combatSpread = request.objectId == 8 && bot->IsInCombat();
            if (combatSpread)
            {
                // A short tactical move owns combat movement, while attacks and
                // native roles remain available. Melee must not immediately chase
                // back into the same pile before the requested spread completes.
                value->Load("spread_hold");
                location = value->Get()->GetLocation();
                auto& positions = ai->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get();
                positions["stay"].Set(location.GetPositionX(), location.GetPositionY(), location.GetPositionZ(),
                                      location.GetMapId());
                positions["return"] = positions["stay"];
                ai->ChangeStrategy("+stay,-follow", BOT_STATE_COMBAT);
                mailbox.hold = request;
                mailbox.releaseAt = Now.load() + (request.durationMs ? request.durationMs : 10000);
                // Continue wide following after the tactical hold expires.
                mailbox.priorMovement->formation = "spread";
            }
            else if (request.objectId == 9)
            {
                if (!mailbox.priorPassive)
                    mailbox.priorPassive = {ai->HasStrategy("passive", BOT_STATE_NON_COMBAT),
                                            ai->HasStrategy("passive", BOT_STATE_COMBAT)};
                StayChatShortcutAction(ai, true).Execute(Event("dialogue"));
                auto& positions = ai->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get();
                positions["stay"].Set(location.GetPositionX(), location.GetPositionY(), location.GetPositionZ(),
                                      location.GetMapId());
                positions["return"] = positions["stay"];
                mailbox.hold = request;
                mailbox.releaseAt = request.durationMs ? Now.load() + request.durationMs : 0;
                ai->ChangeStrategy("+passive", BOT_STATE_COMBAT);
                bot->AttackStop();
                mailbox.preparedReady = false;
            }
            else
                FollowChatShortcutAction(ai, true).Execute(Event("dialogue"));
            DialogueApproach(ai).Place(location, request.objectId == 8 && bot->IsInCombat());
            mailbox.moving = request;
            Record(mailbox, request, Status::Started, "native_spaced_movement_started");
            return;
        }
        Record(mailbox, request,
               loaded && value->Get()->getName() == names[request.objectId] ? Status::Completed : Status::Failed,
               "formation_state");
        return;
    }
    if (!mailbox.hold || !mailbox.priorMovement)
        mailbox.priorMovement = CaptureMovement(ai);
    // A newer movement order owns the timer; an older expiry can never release it.
    mailbox.releaseAt = 0;
    mailbox.hold.reset();
    bool changed = false;
    switch (request.kind)
    {
        case Kind::Stay:
            changed = StayChatShortcutAction(ai, true).Execute(Event("dialogue"));
            if (changed)
            {
                changed = ai->HasStrategy("stay", BOT_STATE_NON_COMBAT) && ai->HasStrategy("stay", BOT_STATE_COMBAT) &&
                          !bot->isMoving();
                mailbox.hold = request;
                mailbox.releaseAt = request.durationMs ? Now.load() + request.durationMs : 0;
            }
            break;
        case Kind::Follow:
        case Kind::Regroup:
            changed = FollowChatShortcutAction(ai, true).Execute(Event("dialogue")) &&
                      ai->HasStrategy("follow", BOT_STATE_NON_COMBAT);
            if (changed && request.kind == Kind::Regroup)
                mailbox.moving = request;
            break;
        case Kind::Retreat:
            changed = FleeChatShortcutAction(ai, true).Execute(Event("dialogue")) &&
                      ai->HasStrategy("passive", BOT_STATE_COMBAT);
            if (changed)
                mailbox.moving = request;
            break;
        case Kind::Approach:
        {
            auto target = ObjectAccessor::GetUnit(*bot, ObjectGuid(request.target.guid));
            if (PlayerbotDialogueBridge::Matches(target, request.target) && target->IsAlive() && bot->IsInMap(target) &&
                bot->InSamePhase(target) && bot->CanSeeOrDetect(target) && bot->IsWithinDistInMap(target, 60.0f))
            {
                changed = StayChatShortcutAction(ai, true).Execute(Event("dialogue"));
                if (changed)
                {
                    // Native stay tracks the destination while native movement handles pathfinding.
                    auto& positions = ai->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get();
                    positions["stay"].Set(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(),
                                          target->GetMapId());
                    positions["return"] = positions["stay"];
                    changed = bot->IsWithinDistInMap(target, 3.0f) || DialogueApproach(ai).Approach(target);
                    if (changed)
                        mailbox.moving = request;
                }
            }
            break;
        }
        default:
            break;
    }
    if (!changed)
    {
        RestoreMovement(ai, mailbox);
        Record(mailbox, request, Status::Failed, "native_movement_refused");
    }
    else
        Record(mailbox, request, mailbox.moving ? Status::Started : Status::Completed,
               mailbox.moving ? "native_movement_started" : "movement_order_established");
}
}  // namespace

void PlayerbotDialogueBridge::Configure(bool enabled)
{
    if (::Enabled.exchange(enabled) && !enabled)
        Suspend();
}
bool PlayerbotDialogueBridge::Enabled() { return ::Enabled.load(); }

bool PlayerbotDialogueBridge::AllowsInitiativeApproach(Player* bot)
{
    auto ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
    if (!ai || bot->IsInCombat() || ai->HasStrategy("stay", BOT_STATE_NON_COMBAT) ||
        ai->HasStrategy("guard", BOT_STATE_NON_COMBAT))
        return false;
    auto mailbox = FindMailbox(bot->GetGUID().GetRawValue());
    if (!mailbox)
        return true;
    std::lock_guard lock(mailbox->mutex);
    return !mailbox->hold && !mailbox->moving && !mailbox->pending;
}

std::vector<PlayerbotDialogue::InventoryItem> PlayerbotDialogueBridge::SavedEquipment(uint64_t bot)
{
    auto mailbox = FindMailbox(bot);
    if (mailbox)
    {
        std::lock_guard lock(mailbox->mutex);
        if (!mailbox->performance.equipment.empty())
            return mailbox->performance.equipment;
        return SavedPerformanceEquipment(ObjectAccessor::FindPlayer(ObjectGuid(bot)));
    }
    return SavedPerformanceEquipment(ObjectAccessor::FindPlayer(ObjectGuid(bot)));
}

Status PlayerbotDialogueBridge::PreparationStatus(uint64_t bot, std::string const& group, uint64_t authorityVersion)
{
    auto mailbox = FindMailbox(bot);
    if (!mailbox)
        return Status::Failed;
    std::lock_guard lock(mailbox->mutex);
    if (mailbox->revision != authorityVersion || !mailbox->hold || mailbox->hold->groupId != group ||
        mailbox->hold->kind != Kind::Formation || mailbox->hold->objectId != 9)
        return Status::Failed;
    return mailbox->preparedReady ? Status::Completed : Status::Pending;
}

void PlayerbotDialogueBridge::CompletePreparedPull(Request const& request)
{
    for (auto const& companion : request.preparedParty)
        if (auto mailbox = FindMailbox(companion.actor.guid))
        {
            std::lock_guard lock(mailbox->mutex);
            if (mailbox->revision == companion.authorityVersion && mailbox->hold &&
                mailbox->hold->groupId == request.groupId && mailbox->hold->kind == Kind::Formation &&
                mailbox->hold->objectId == 9)
                mailbox->completedPull = request.groupId;
        }
}

bool PlayerbotDialogueBridge::IsSettingUp(Player* player)
{
    return player && PlayerbotFactory::IsSettingUp(player->GetGUID());
}

bool PlayerbotDialogueBridge::HasPendingWork(uint64_t bot)
{
    auto mailbox = FindMailbox(bot);
    if (!mailbox)
        return false;
    std::lock_guard lock(mailbox->mutex);
    return mailbox->pending || mailbox->moving || mailbox->combat || mailbox->inventory ||
        mailbox->service || mailbox->social || mailbox->supplyOffer || mailbox->duel ||
        mailbox->specialization || mailbox->hold;
}

std::optional<Request> PlayerbotDialogueBridge::PendingOffer(uint64_t bot, uint64_t human)
{
    auto mailbox = FindMailbox(bot);
    if (!mailbox)
        return {};
    std::lock_guard lock(mailbox->mutex);
    auto const& offer = mailbox->supplyOffer;
    if (offer && offer->requester.guid == human && offer->authorityVersion == mailbox->revision &&
        Now.load() < offer->deadlineMs)
        return offer;
    return {};
}

bool PlayerbotDialogueBridge::AllowsInitiative(Player* bot, Player* observer)
{
    auto ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
    if (!Enabled() || !ai || !observer || !observer->IsInWorld() || !bot->IsInMap(observer) ||
        !bot->InSamePhase(observer) || !bot->IsAlive() || !observer->IsAlive() || !bot->IsFriendlyTo(observer) ||
        !bot->IsWithinDistInMap(observer, 25.0f) || !bot->CanSeeOrDetect(observer) || !bot->IsWithinLOSInMap(observer))
        return false;
    auto master = ai->GetMaster();
    if (master && master != observer && master->GetSession() && !master->GetSession()->IsBot())
        return false;  // Another human's companion cannot volunteer away their authority.
    return ai->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_TALK, true, observer);
}

bool PlayerbotDialogueBridge::AllowsInvitation(Player* bot, Player* observer)
{
    auto ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
    if (!Enabled() || !ai || !observer || !observer->IsInWorld() || !bot->IsInMap(observer) ||
        !bot->InSamePhase(observer) || !bot->IsAlive() || !observer->IsAlive() || !bot->IsFriendlyTo(observer) ||
        !bot->IsWithinDistInMap(observer, 60.0f) || !bot->CanSeeOrDetect(observer) || bot->IsInCombat() ||
        observer->IsInCombat() || bot->GetGroup() || bot->GetGroupInvite() || observer->GetGroup() ||
        observer->GetGroupInvite())
        return false;
    auto master = ai->GetMaster();
    if (master && master != observer && master->GetSession() && !master->GetSession()->IsBot())
        return false;
    // A normal invitation has no spell line-of-sight requirement. It remains an
    // offer, requires human acceptance and never changes somebody else's party.
    return ai->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_TALK, true, observer);
}

void PlayerbotDialogueBridge::Suspend()
{
    std::vector<uint64_t> actors;
    {
        std::lock_guard lock(MailboxesMutex);
        for (auto const& [actor, mailbox] : Mailboxes)
            actors.push_back(actor);
    }
    for (auto actor : actors)
        Interrupt(actor);
}
void PlayerbotDialogueBridge::SetTime(uint64_t nowMs) { Now.store(nowMs); }

Entity PlayerbotDialogueBridge::Snapshot(Unit* unit)
{
    // Identity tracking also supports dialogue without gameplay actions enabled.
    if (!unit)
        return {};
    std::lock_guard lock(LivesMutex);
    auto key = std::make_tuple(unit->GetGUID().GetRawValue(), unit->GetMapId(), unit->GetInstanceId());
    auto [it, added] = Lives.try_emplace(key, 0);
    if (added)
        it->second = ++NextLife;
    return {unit->GetGUID().GetRawValue(), unit->GetMapId(), unit->GetInstanceId(), unit->GetEntry(), it->second};
}

bool PlayerbotDialogueBridge::Matches(Unit* unit, Entity const& entity)
{
    if (!unit || !unit->IsInWorld() || unit->GetGUID().GetRawValue() != entity.guid || unit->GetMapId() != entity.map ||
        unit->GetInstanceId() != entity.instance || unit->GetEntry() != entity.entry)
        return false;
    return entity.incarnation && Snapshot(unit).incarnation == entity.incarnation;
}

void PlayerbotDialogueBridge::InvalidateLife(Unit* unit)
{
    // Retire lives even while actions are disabled; re-enabling must not revive
    // an old reference to a creature that died or despawned in the meantime.
    if (!unit)
        return;
    std::lock_guard lock(LivesMutex);
    auto found = Lives.find({unit->GetGUID().GetRawValue(), unit->GetMapId(), unit->GetInstanceId()});
    if (found != Lives.end())
        Lives.erase(found);  // A future snapshot receives a new token; retired spawns need no retained record.
}

void PlayerbotDialogueBridge::RegisterScripts()
{
    new DialogueCreatureLife();
    new DialogueUnitLife();
    new DialogueResurrection();
}

uint64_t PlayerbotDialogueBridge::Revision(uint64_t bot)
{
    if (!Enabled())
        return 0;
    auto mailbox = FindMailbox(bot, true);
    std::lock_guard lock(mailbox->mutex);
    return mailbox->revision;
}

bool PlayerbotDialogueBridge::Enqueue(Request const& request)
{
    if (!Enabled())
        return false;
    auto mailbox = FindMailbox(request.actor.guid, true);
    std::lock_guard lock(mailbox->mutex);
    if (request.authorityVersion != mailbox->revision || request.sequence < mailbox->latestSequence || mailbox->pending)
        return false;
    mailbox->latestSequence = request.sequence;
    mailbox->pending = request;
    return true;
}

void PlayerbotDialogueBridge::Cancel(uint64_t bot, std::string const& requestId)
{
    if (auto mailbox = FindMailbox(bot))
    {
        std::lock_guard lock(mailbox->mutex);
        if (mailbox->pending && mailbox->pending->id == requestId)
        {
            Record(*mailbox, *mailbox->pending, Status::Cancelled, "cancelled_before_native_execution");
            mailbox->pending.reset();
        }
        if (mailbox->duel && mailbox->duel->request.id == requestId)
            QueueCombatCancellation(*mailbox, "cancelled");
        if (mailbox->social && mailbox->social->id == requestId)
            QueueCombatCancellation(*mailbox, "cancelled");
        if (mailbox->combat && mailbox->combat->request.id == requestId)
            QueueCombatCancellation(*mailbox, "cancelled");
        if (mailbox->inventory && mailbox->inventory->request.id == requestId)
            QueueCombatCancellation(*mailbox, "cancelled");
        if (mailbox->service && mailbox->service->id == requestId)
            QueueCombatCancellation(*mailbox, "cancelled");
        if (mailbox->specialization && mailbox->specialization->request.id == requestId)
            QueueCombatCancellation(*mailbox, "cancelled");
        if (mailbox->moving && mailbox->moving->id == requestId)
        {
            Record(*mailbox, *mailbox->moving, Status::Cancelled, "cancelled");
            mailbox->moving.reset();
            // Restoration occurs on the owning map thread, never in this caller.
            mailbox->restore = true;
        }
    }
}

void PlayerbotDialogueBridge::Interrupt(uint64_t bot)
{
    if (auto mailbox = FindMailbox(bot))
    {
        std::lock_guard lock(mailbox->mutex);
        ++mailbox->revision;
        QueueCombatCancellation(*mailbox, "ownership_changed");
        for (auto request : {mailbox->pending, mailbox->moving})
            if (request)
                Record(*mailbox, *request, Status::Cancelled, "explicit_command_or_ownership_changed");
        mailbox->pending.reset();
        mailbox->moving.reset();
        mailbox->hold.reset();
        mailbox->restore = true;
        mailbox->releaseAt = 0;
    }
}

void PlayerbotDialogueBridge::ExplicitCommand(uint64_t bot, std::string const& command)
{
    if (command == "co ?" || command == "nc ?" || command == "dead ?")
        return;  // Native strategy inspection changes no order or pending authority.
    if (auto mailbox = FindMailbox(bot))
    {
        std::lock_guard lock(mailbox->mutex);
        ++mailbox->revision;
        QueueCombatCancellation(*mailbox, "explicit_command");
        bool wasMoving = mailbox->moving.has_value();
        for (auto request : {mailbox->pending, mailbox->moving})
            if (request)
                Record(*mailbox, *request, Status::Cancelled, "explicit_command");
        mailbox->pending.reset();
        mailbox->moving.reset();
        auto verb = command.substr(0, command.find(' '));
        bool movement = verb == "follow" || verb == "stay" || verb == "flee" || verb == "guard" || verb == "free" ||
                        verb == "reset" || verb == "runaway" || verb == "formation" || verb == "nc" || verb == "co" ||
                        verb == "do" || verb == "d";
        if (movement)
        {
            // The native command now owns movement; do not restore an older order over it.
            mailbox->hold.reset();
            mailbox->priorMovement.reset();
            mailbox->releaseAt = 0;
            mailbox->restore = false;
            mailbox->priorPassive.reset();
        }
        else if (mailbox->hold)
            mailbox->hold->authorityVersion = mailbox->revision;
        else if (wasMoving)
            mailbox->restore = true;
    }
}

void PlayerbotDialogueBridge::ExecuteSocial(Request const& request)
{
    auto mailbox = FindMailbox(request.actor.guid);
    if (!mailbox)
        return;
    auto bot = ObjectAccessor::FindPlayer(ObjectGuid(request.actor.guid));
    auto ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
    {
        std::lock_guard lock(mailbox->mutex);
        if (!mailbox->social || mailbox->social->id != request.id)
            return;
        mailbox->social.reset();
        if (!Enabled() || !bot || !ai || !Valid(bot, ai, *mailbox, request) || Now.load() >= request.deadlineMs)
        {
            Record(*mailbox, request, Status::Failed, "authority_or_world_changed");
            return;
        }
    }
    // Normal group handlers can call ownership hooks, which interrupt this
    // mailbox. No lock or pending entry may survive across that synchronous call.
    auto outcome = RunSocial(bot, request, Now.load());
    std::lock_guard lock(mailbox->mutex);
    mailbox->outcomes.push_back(std::move(outcome));
}

void PlayerbotDialogueBridge::Update(Player* bot)
{
    if (!bot || (!Enabled() && !HasMailboxes.load()))
        return;
    auto mailbox = FindMailbox(bot->GetGUID().GetRawValue());
    auto ai = GET_PLAYERBOT_AI(bot);
    if (!mailbox || !ai)
        return;
    std::lock_guard lock(mailbox->mutex);
    if (!mailbox->completedPull.empty())
    {
        if (mailbox->hold && mailbox->hold->groupId == mailbox->completedPull &&
            Valid(bot, ai, *mailbox, *mailbox->hold))
        {
            RestoreMovement(ai, *mailbox);
            RestorePassive(ai, *mailbox);
        }
        mailbox->completedPull.clear();
    }
    for (auto const& execution : mailbox->cancelCombat)
    {
        CancelCombat(ai, execution.request);
        FinishPartyPull(ai, execution);
    }
    mailbox->cancelCombat.clear();
    for (auto const& execution : mailbox->cancelSpecs)
        CancelSpec(bot, execution);
    mailbox->cancelSpecs.clear();
    FinishInventoryCancellations(bot, *mailbox);
    for (auto const& execution : mailbox->cancelDuels)
        CancelDuel(bot, execution);
    mailbox->cancelDuels.clear();
    if (mailbox->stopServiceMovement)
    {
        bot->StopMoving();
        bot->GetMotionMaster()->Clear();
        mailbox->stopServiceMovement = false;
    }
    if (mailbox->service)
    {
        auto const& request = *mailbox->service;
        auto outcome = Valid(bot, ai, *mailbox, request)
                           ? RunService(bot, ai, request, Now.load())
                           : Outcome{request.id, Status::Failed, "authority_or_world_changed", Now.load()};
        if (Terminal(outcome.status))
        {
            if (outcome.detail == "native_talents_rebuilt" || outcome.detail == "native_talent_build_incomplete")
                RefreshCombat(ai);
            mailbox->outcomes.push_back(outcome);
            mailbox->service.reset();
        }
    }
    if (mailbox->inventory)
    {
        auto& execution = *mailbox->inventory;
        auto outcome = PollInventory(bot, execution, Now.load());
        if (!Terminal(outcome.status) && !Valid(bot, ai, *mailbox, execution.request))
            outcome = {execution.request.id, Status::Failed, "authority_or_world_changed", Now.load()};
        if (Terminal(outcome.status))
        {
            CancelInventory(bot, execution.request);
            mailbox->outcomes.push_back(outcome);
            mailbox->inventory.reset();
        }
    }
    if (mailbox->combat)
    {
        auto& execution = *mailbox->combat;
        auto outcome = Valid(bot, ai, *mailbox, execution.request)
                           ? PollCombat(bot, ai, execution, Now.load())
                           : Outcome{execution.request.id, Status::Failed, "authority_or_world_changed", Now.load()};
        if (Terminal(outcome.status))
        {
            if (outcome.status != Status::Completed)
                CancelCombat(ai, execution.request);
            FinishPartyPull(ai, execution);
            mailbox->outcomes.push_back(outcome);
            mailbox->combat.reset();
        }
    }
    if (mailbox->duel)
    {
        auto& execution = *mailbox->duel;
        auto outcome = PollDuel(bot, execution, Now.load());
        if (!Terminal(outcome.status) && !Valid(bot, ai, *mailbox, execution.request))
            outcome = {execution.request.id, Status::Failed, "authority_or_world_changed", Now.load()};
        if (Terminal(outcome.status))
        {
            if (outcome.status != Status::Completed)
                CancelDuel(bot, execution);
            mailbox->outcomes.push_back(outcome);
            mailbox->duel.reset();
        }
    }
    if (mailbox->specialization)
    {
        auto const& execution = *mailbox->specialization;
        auto outcome = Valid(bot, ai, *mailbox, execution.request)
                           ? PollSpec(bot, execution, Now.load())
                           : Outcome{execution.request.id, Status::Failed, "authority_or_world_changed", Now.load()};
        if (Terminal(outcome.status))
        {
            if (outcome.status == Status::Completed)
            {
                RefreshCombat(ai);
            }
            else
                CancelSpec(bot, execution);
            mailbox->outcomes.push_back(outcome);
            mailbox->specialization.reset();
        }
    }
    if (mailbox->restore || (mailbox->releaseAt && Now.load() >= mailbox->releaseAt))
    {
        bool restoreAll = mailbox->restore;
        if (restoreAll)
        {
            StopPerformance(bot, mailbox->performance);
            mailbox->performance.ownsMovement = false;
        }
        RestoreMovement(ai, *mailbox);
        if (restoreAll)
            RestorePassive(ai, *mailbox);
        mailbox->restore = false;
    }
    if (UpdatePerformance(bot, mailbox->performance, Now.load()) && mailbox->performance.ownsMovement)
    {
        RestoreMovement(ai, *mailbox);
        mailbox->performance.ownsMovement = false;
    }
    if (mailbox->hold && !Valid(bot, ai, *mailbox, *mailbox->hold))
        RestoreMovement(ai, *mailbox);
    if (mailbox->hold && mailbox->hold->kind == Kind::Formation && mailbox->hold->objectId == 9)
    {
        auto location = ai->GetAiObjectContext()->GetValue<Formation*>("formation")->Get()->GetLocation();
        mailbox->preparedReady =
            !Formation::IsNullLocation(location) &&
            bot->GetExactDist(location.GetPositionX(), location.GetPositionY(), location.GetPositionZ()) <= 1.5f &&
            !bot->isMoving();
    }
    if (!Enabled())
        return;  // Disabled bridges finish cleanup, but cannot execute new requests.
    if (mailbox->pending)
    {
        auto request = std::move(*mailbox->pending);
        mailbox->pending.reset();
        Start(bot, ai, *mailbox, request);
    }
    if (mailbox->moving)
    {
        auto const& request = *mailbox->moving;
        if (request.kind == Kind::Formation && request.objectId >= 8)
        {
            auto formation = ai->GetAiObjectContext()->GetValue<Formation*>("formation")->Get();
            auto location = formation->GetLocation();
            if (!Valid(bot, ai, *mailbox, request) || Formation::IsNullLocation(location) ||
                Now.load() >= request.deadlineMs)
            {
                Record(*mailbox, request, Status::Failed, "spaced_position_or_authority_lost");
                mailbox->moving.reset();
                RestoreMovement(ai, *mailbox);
            }
            else if (bot->GetExactDist(location.GetPositionX(), location.GetPositionY(), location.GetPositionZ()) <=
                     1.5f)
            {
                Record(*mailbox, request, Status::Completed, "native_spaced_position_observed");
                mailbox->moving.reset();
            }
            else if (!bot->isMoving())
                DialogueApproach(ai).Place(location, request.objectId == 8 && bot->IsInCombat());
            return;
        }
        auto target = ObjectAccessor::GetUnit(
            *bot, ObjectGuid(request.kind == Kind::Approach ? request.target.guid : request.requester.guid));
        auto const& targetEntity = request.kind == Kind::Approach ? request.target : request.requester;
        if (!Valid(bot, ai, *mailbox, request) || !Matches(target, targetEntity) || !target->IsAlive() ||
            !bot->IsInMap(target) || !bot->InSamePhase(target) || Now.load() >= request.deadlineMs)
        {
            Record(*mailbox, request, Status::Failed, "movement_target_or_authority_lost");
            mailbox->moving.reset();
            RestoreMovement(ai, *mailbox);
        }
        else if (bot->GetExactDist(target) <= 4.0f)
        {
            Record(*mailbox, request, Status::Completed, "arrival_observed");
            bool personal = request.voluntary;
            mailbox->moving.reset();
            if (personal)
                RestoreMovement(ai, *mailbox);
        }
        else if (request.kind == Kind::Regroup && !bot->isMoving())
        {
            // Native follow can stop at its formation radius before our arrival
            // threshold. Close the remaining gap without changing that formation.
            DialogueApproach(ai).Approach(target);
        }
    }
}

void PlayerbotDialogueBridge::Release(Player* bot)
{
    if (!bot)
        return;
    InvalidateLife(bot);
    Interrupt(bot->GetGUID().GetRawValue());
    auto mailbox = FindMailbox(bot->GetGUID().GetRawValue());
    auto ai = GET_PLAYERBOT_AI(bot);
    if (!mailbox || !ai)
        return;
    std::lock_guard lock(mailbox->mutex);
    StopPerformance(bot, mailbox->performance);
    if (mailbox->performance.equipment.empty())
        mailbox->performance.equipment = SavedPerformanceEquipment(bot);
    if (!mailbox->performance.equipment.empty())
    {
        Request restore;
        restore.kind = Kind::RestoreEquipment;
        Perform(bot, restore, mailbox->performance, Now.load());
    }
    RestoreMovement(ai, *mailbox);
    RestorePassive(ai, *mailbox);
    for (auto const& execution : mailbox->cancelCombat)
    {
        CancelCombat(ai, execution.request);
        FinishPartyPull(ai, execution);
    }
    mailbox->cancelCombat.clear();
    for (auto const& execution : mailbox->cancelSpecs)
        CancelSpec(bot, execution);
    mailbox->cancelSpecs.clear();
    FinishInventoryCancellations(bot, *mailbox);
    for (auto const& execution : mailbox->cancelDuels)
        CancelDuel(bot, execution);
    mailbox->cancelDuels.clear();
    mailbox->restore = false;
}

void PlayerbotDialogueBridge::FilterPersistence(Player* bot, std::vector<std::string>& values,
                                                std::vector<std::string>& combat, std::vector<std::string>& nonCombat)
{
    auto mailbox = FindMailbox(bot->GetGUID().GetRawValue());
    auto ai = GET_PLAYERBOT_AI(bot);
    if (!mailbox || !ai)
        return;
    std::lock_guard lock(mailbox->mutex);
    if (!mailbox->priorMovement && !mailbox->priorPassive)
        return;
    // Filter copies rather than changing the live AI just to serialize it. Keep unrelated
    // strategy/value edits, but never save a temporary PBC movement override across a crash.
    auto restore = [](std::vector<std::string>& strategies, std::string const& modifiers)
    {
        for (auto const& modifier : split(modifiers, ','))
        {
            if (modifier.empty())
                continue;
            auto name = modifier.substr(1);
            std::erase(strategies, name);
            if (modifier.front() == '+')
                strategies.push_back(name);
        }
    };
    if (mailbox->priorMovement)
    {
        restore(combat, mailbox->priorMovement->combat);
        restore(nonCombat, mailbox->priorMovement->nonCombat);
        std::erase_if(values, [](auto const& value) { return value.starts_with("formation>"); });
        values.push_back("formation>" + mailbox->priorMovement->formation);
    }
    if (mailbox->priorPassive)
    {
        restore(nonCombat, mailbox->priorPassive->first ? "+passive" : "-passive");
        restore(combat, mailbox->priorPassive->second ? "+passive" : "-passive");
    }
    if (!mailbox->priorMovement)
        return;
    PositionValue position(ai);
    position.Get() = ai->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get();
    position.Get()["stay"] = mailbox->priorMovement->stay;
    position.Get()["return"] = mailbox->priorMovement->returned;
    std::erase_if(values, [](auto const& value) { return value.starts_with("position>"); });
    values.push_back("position>" + position.Save());
}

std::vector<Outcome> PlayerbotDialogueBridge::Drain()
{
    std::vector<std::shared_ptr<Mailbox>> mailboxes;
    {
        std::lock_guard lock(MailboxesMutex);
        for (auto const& [id, mailbox] : Mailboxes)
            mailboxes.push_back(mailbox);
    }
    std::vector<Outcome> result;
    for (auto const& mailbox : mailboxes)
    {
        std::lock_guard lock(mailbox->mutex);
        for (auto& outcome : mailbox->outcomes)
            result.push_back(std::move(outcome));
        mailbox->outcomes.clear();
    }
    return result;
}
