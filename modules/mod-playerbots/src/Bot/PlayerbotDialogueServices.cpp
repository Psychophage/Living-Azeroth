/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: quoted services use native money, vendor and trainer rules.
#include "PlayerbotDialogueServices.h"

#include <cmath>
#include <set>

#include "Creature.h"
#include "Item.h"
#include "ItemRepair.h"
#include "MovementActions.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PlayerbotDialogue.h"
#include "PlayerbotDialogueInventory.h"
#include "PlayerbotDialogueTalents.h"
#include "Playerbots.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Trainer.h"
#include "World.h"
#include "WorldPacket.h"

namespace PlayerbotDialogue
{
namespace
{
bool KnownTraining(Player* bot, uint32 id)
{
    auto spell = sSpellMgr->GetSpellInfo(id);
    if (!spell)
        return false;
    bool wrapped = false;
    for (auto const& effect : spell->Effects)
        if (effect.Effect == SPELL_EFFECT_LEARN_SPELL && effect.TriggerSpell)
        {
            wrapped = true;
            if (!bot->HasSpell(effect.TriggerSpell))
                return false;
        }
    return wrapped || bot->HasSpell(id);
}

bool ProviderVisible(Player* bot, Creature* provider)
{
    return provider && provider->IsAlive() && bot->IsInMap(provider) && bot->InSamePhase(provider) &&
           bot->CanSeeOrDetect(provider) && !bot->IsHostileTo(provider) && bot->IsWithinDistInMap(provider, 60.0f);
}

class ReachService final : public MovementAction
{
public:
    explicit ReachService(PlayerbotAI* ai) : MovementAction(ai, "dialogue service range") {}
    bool Reach(Creature* target) { return IsMovingAllowed(target) && MoveNear(target, 2.0f); }
};
}  // namespace

std::optional<uint64_t> QuoteService(Player* bot, Creature* provider, Request const& request)
{
    if (!ProviderVisible(bot, provider))
        return std::nullopt;
    float discount = bot->GetReputationPriceDiscount(provider);
    uint64_t total = 0;
    if (request.kind == Kind::Repair)
    {
        if (!provider->HasNpcFlag(UNIT_NPC_FLAG_REPAIR))
            return std::nullopt;
        for (auto const& observed : request.items)
        {
            auto item = bot->GetItemByPos(observed.position);
            if (!item || item->GetGUID().GetRawValue() != observed.guid || item->IsInTrade())
                return std::nullopt;
            auto price = CalculateItemRepairCost(item, discount);
            if (!price)
                return std::nullopt;
            total += *price;
        }
    }
    else if (request.kind == Kind::Train)
    {
        auto ai = GET_PLAYERBOT_AI(bot);
        auto trainer = sObjectMgr->GetTrainer(provider->GetEntry());
        if (!ai || !ai->IsAltBot() || !provider->HasNpcFlag(UNIT_NPC_FLAG_TRAINER) || !trainer ||
            !trainer->IsTrainerValidForPlayer(bot) || request.trainingSpells.empty())
            return std::nullopt;
        std::set<uint32_t> seen;
        for (auto id : request.trainingSpells)
        {
            if (!seen.insert(id).second)
                return std::nullopt;
            if (KnownTraining(bot, id))
                continue;
            auto offered = trainer->GetSpell(id);
            if (!offered || !trainer->CanTeachSpell(bot, offered))
                return std::nullopt;
            total += uint32_t(offered->MoneyCost * discount);
        }
    }
    else if (request.kind == Kind::Talents)
    {
        if (!provider->HasNpcFlag(UNIT_NPC_FLAG_TRAINER) || !provider->CanResetTalents(bot) ||
            request.talentSpec != bot->GetActiveSpec() || request.talentPlan.empty() ||
            TalentPlan(bot, request.objectId) != request.talentPlan)
            return std::nullopt;
        total = HasAllocatedTalents(bot) && !sWorld->getBoolConfig(CONFIG_NO_RESET_TALENT_COST)
                    ? bot->resetTalentsCost()
                    : 0;
    }
    else if (request.kind == Kind::Buy)
    {
        auto inventory = provider->GetVendorItems();
        auto offered = inventory ? inventory->GetItem(request.vendorSlot) : nullptr;
        auto item = sObjectMgr->GetItemTemplate(request.objectId);
        // Tokens/honour are separate currencies, never implicitly authorized by a gold cap.
        if (!provider->HasNpcFlag(UNIT_NPC_FLAG_VENDOR) || !offered || offered->item != request.objectId || !item ||
            offered->ExtendedCost || !item->BuyCount || !request.quantity || request.quantity % item->BuyCount)
            return std::nullopt;
        uint64_t bundles = request.quantity / item->BuyCount;
        if (bundles > 255 || uint64_t(std::max(0, item->BuyPrice)) * bundles > MAX_MONEY_AMOUNT)
            return std::nullopt;
        total = item->BuyPrice > 0 ? uint64_t(std::floor(float(item->BuyPrice * bundles) * discount)) : 0;
    }
    else
        return std::nullopt;
    return total <= MAX_MONEY_AMOUNT ? std::optional<uint64_t>(total) : std::nullopt;
}

std::vector<ServiceOffer> ServiceOffers(Player* bot, Creature* provider, bool train, bool repair, bool buy,
                                        bool talents)
{
    std::vector<ServiceOffer> result;
    if (!ProviderVisible(bot, provider))
        return result;
    auto snapshot = PlayerbotDialogueBridge::Snapshot(provider);
    if (repair && provider->HasNpcFlag(UNIT_NPC_FLAG_REPAIR))
    {
        Request request;
        request.kind = Kind::Repair;
        request.items = InventorySnapshot(GET_PLAYERBOT_AI(bot));
        if (auto cost = QuoteService(bot, provider, request))
            result.push_back({Kind::Repair, snapshot, 0, 0, 1, *cost, 0, 1.0f, "Repair carried equipment"});
    }
    if (train && provider->HasNpcFlag(UNIT_NPC_FLAG_TRAINER))
        if (auto trainer = sObjectMgr->GetTrainer(provider->GetEntry());
            trainer && trainer->IsTrainerValidForPlayer(bot))
            for (auto const& spell : trainer->GetSpells())
            {
                Request request;
                request.kind = Kind::Train;
                request.trainingSpells = {spell.SpellId};
                if (!KnownTraining(bot, spell.SpellId))
                    if (auto cost = QuoteService(bot, provider, request))
                    {
                        auto info = sSpellMgr->GetSpellInfo(spell.SpellId);
                        result.push_back({Kind::Train, snapshot, spell.SpellId, 0, 1, *cost, 0, 1.0f,
                                          std::string(info->SpellName[LOCALE_enUS]) + " " + info->Rank[LOCALE_enUS]});
                    }
            }
    if (talents && provider->HasNpcFlag(UNIT_NPC_FLAG_TRAINER) && provider->CanResetTalents(bot))
        for (auto const& [id, name] : AvailableTalentPresets(bot))
        {
            Request request;
            request.kind = Kind::Talents;
            request.objectId = id;
            request.talentSpec = bot->GetActiveSpec();
            request.talentPlan = TalentPlan(bot, id);
            if (auto cost = QuoteService(bot, provider, request))
            {
                ServiceOffer offer{Kind::Talents, snapshot, id, 0, 1, *cost, 0, 1.0f, name};
                offer.talentSpec = request.talentSpec;
                offer.talentPlan = std::move(request.talentPlan);
                result.push_back(std::move(offer));
            }
        }
    if (buy && provider->HasNpcFlag(UNIT_NPC_FLAG_VENDOR))
        if (auto inventory = provider->GetVendorItems())
            for (uint32_t slot = 0; slot < inventory->GetItemCount(); ++slot)
            {
                auto offered = inventory->GetItem(slot);
                auto item = offered ? sObjectMgr->GetItemTemplate(offered->item) : nullptr;
                if (!item || offered->ExtendedCost)
                    continue;
                Request request;
                request.kind = Kind::Buy;
                request.objectId = item->ItemId;
                request.quantity = item->BuyCount;
                request.vendorSlot = slot;
                if (auto cost = QuoteService(bot, provider, request))
                    result.push_back({Kind::Buy, snapshot, item->ItemId, slot, item->BuyCount, *cost,
                                      uint32_t(std::max(0, item->BuyPrice)), bot->GetReputationPriceDiscount(provider),
                                      item->Name1});
            }
    return result;
}

Outcome RunService(Player* bot, PlayerbotAI* ai, Request const& request, uint64_t nowMs)
{
    auto result = [&](Status status, std::string detail, uint64_t spent = 0)
    { return Outcome{request.id, status, std::move(detail), nowMs, spent}; };
    auto unit = ObjectAccessor::GetUnit(*bot, ObjectGuid(request.target.guid));
    auto provider = unit ? unit->ToCreature() : nullptr;
    if (!PlayerbotDialogueBridge::Matches(provider, request.target) || !ProviderVisible(bot, provider) ||
        bot->IsInCombat() || bot->GetTradeData() || nowMs >= request.deadlineMs)
        return result(Status::Failed, "service_target_or_state_unavailable");
    auto price = QuoteService(bot, provider, request);
    if (!price || request.moneyBudget.empty() || *price > request.reservedCopper ||
        request.reservedCopper > request.copperLimit || bot->GetMoney() < *price)
        return result(Status::Failed, "service_quote_changed_or_insufficient_funds");
    if (!bot->IsWithinDistInMap(provider, INTERACTION_DISTANCE))
    {
        // MoveNear also returns false while the native movement throttle is
        // waiting on the previous path. Recheck on later owning-map ticks;
        // the request deadline bounds an actually unreachable provider.
        ReachService(ai).Reach(provider);
        return result(Status::Started, "approaching_service");
    }
    uint32 flag = request.kind == Kind::Repair                                     ? UNIT_NPC_FLAG_REPAIR
                  : (request.kind == Kind::Train || request.kind == Kind::Talents) ? UNIT_NPC_FLAG_TRAINER
                                                                                   : UNIT_NPC_FLAG_VENDOR;
    if (bot->GetNPCIfCanInteractWith(provider->GetGUID(), flag) != provider)
        return result(Status::Failed, "native_service_interaction_refused");
    if (bot->IsNonMeleeSpellCast(false))
        return result(Status::Started, "waiting_for_current_cast");
    uint64_t before = bot->GetMoney();
    bool complete = true;
    if (request.kind == Kind::Repair)
    {
        float discount = bot->GetReputationPriceDiscount(provider);
        for (auto const& observed : request.items)
        {
            bot->DurabilityRepair(observed.position, true, discount, false);
            auto item = bot->GetItemByPos(observed.position);
            complete &=
                item && item->GetUInt32Value(ITEM_FIELD_DURABILITY) == item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
        }
    }
    else if (request.kind == Kind::Train)
    {
        auto trainer = sObjectMgr->GetTrainer(provider->GetEntry());
        for (auto id : request.trainingSpells)
        {
            if (!KnownTraining(bot, id))
                trainer->TeachSpell(provider, bot, id);
            if (!KnownTraining(bot, id))
            {
                complete = false;
                break;  // A refused learning hook can still charge; record that real cost.
            }
        }
    }
    else if (request.kind == Kind::Talents)
    {
        if (HasAllocatedTalents(bot))
        {
            WorldPacket packet(MSG_TALENT_WIPE_CONFIRM, 8);
            packet << provider->GetGUID();
            bot->GetSession()->HandleTalentWipeConfirmOpcode(packet);
        }
        complete = !HasAllocatedTalents(bot) && ApplyTalentPlan(bot, request);
    }
    else
    {
        uint32_t count = bot->GetItemCount(request.objectId, false);
        auto item = sObjectMgr->GetItemTemplate(request.objectId);
        auto session = bot->GetSession();
        auto previousVendor = session->GetCurrentVendor();
        session->SetCurrentVendor(0);  // Bind the observed NPC's own inventory, never a stale gossip vendor.
        bot->BuyItemFromVendorSlot(provider->GetGUID(), request.vendorSlot, request.objectId,
                                   request.quantity / item->BuyCount, NULL_BAG, NULL_SLOT);
        session->SetCurrentVendor(previousVendor);
        complete = bot->GetItemCount(request.objectId, false) == count + request.quantity;
    }
    uint64_t spent = before > bot->GetMoney() ? before - bot->GetMoney() : 0;
    if (spent > request.reservedCopper)
        return result(Status::Failed, "native_service_charged_above_quote", spent);
    return result(complete ? Status::Completed : Status::Failed,
                  request.kind == Kind::Talents
                      ? (complete ? "native_talents_rebuilt" : "native_talent_build_incomplete")
                      : (complete ? "native_service_change_observed" : "native_service_effect_not_observed"),
                  spent);
}
}  // namespace PlayerbotDialogue
