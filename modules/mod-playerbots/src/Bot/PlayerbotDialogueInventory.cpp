/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: native inventory changes, observed before reporting completion.
#include "PlayerbotDialogueInventory.h"

#include <map>
#include <mutex>
#include <set>

#include "Bag.h"
#include "Chat.h"
#include "Event.h"
#include "InventoryAction.h"
#include "ItemPackets.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "PlayerbotDialogue.h"
#include "Playerbots.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "TradeData.h"
#include "TradeStatusAction.h"

namespace PlayerbotDialogue
{
namespace
{
class InventoryReader final : public InventoryAction
{
public:
    explicit InventoryReader(PlayerbotAI* ai) : InventoryAction(ai, "dialogue inventory") {}
    std::vector<Item*> Read()
    {
        CollectItemsVisitor visitor;
        IterateItems(&visitor, IterateItemsMask(ITERATE_ITEMS_IN_BAGS | ITERATE_ITEMS_IN_EQUIP));
        return visitor.items;
    }
};

struct Watch
{
    Request request;
    std::vector<std::pair<uint64_t, uint32_t>> offered;
    uint64_t item = 0;
    uint32_t spell = 0;
    bool used = false;
    bool settled = false;
    bool refreshOffer = false;
    bool closed = false;
};
std::mutex WatchMutex;
std::map<uint64_t, Watch> Watches;

Item* BoundItem(Player* bot, InventoryItem const& observed)
{
    auto item = bot->GetItemByPos(observed.position);
    return item && item->GetGUID().GetRawValue() == observed.guid && item->GetEntry() == observed.entry &&
                   item->GetCount() == observed.count && item->GetItemRandomPropertyId() == observed.property &&
                   !item->IsInTrade()
               ? item
               : nullptr;
}

uint16 EmptyPosition(Player* bot, Item* item, uint32 count)
{
    auto fits = [&](uint8 bag, uint8 slot)
    {
        ItemPosCountVec destinations;
        return !bot->GetItemByPos(bag, slot) &&
               bot->CanStoreNewItem(bag, slot, destinations, item->GetEntry(), count) == EQUIP_ERR_OK;
    };
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (fits(INVENTORY_SLOT_BAG_0, slot))
            return uint16(INVENTORY_SLOT_BAG_0 << 8) | slot;
    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
        if (auto bag = bot->GetBagByPos(slot))
            for (uint8 inBag = 0; inBag < bag->GetBagSize(); ++inBag)
                if (fits(slot, inBag))
                    return uint16(slot << 8) | inBag;
    return 0;
}

bool ExactOffer(Player* bot, Watch const& watch)
{
    auto trade = bot->GetTradeData();
    if (!trade || !trade->GetTrader() || trade->GetTrader()->GetGUID().GetRawValue() != watch.request.target.guid ||
        trade->GetMoney() || trade->GetSpell())
        return false;
    std::map<uint64_t, uint32_t> actual;
    for (uint8 slot = 0; slot < TRADE_SLOT_COUNT; ++slot)
        if (auto item = trade->GetItem(TradeSlots(slot)))
        {
            if (slot >= TRADE_SLOT_TRADED_COUNT ||
                (watch.request.tradeQuantities.empty() ? item->GetEntry() != watch.request.objectId
                                                       : !watch.request.tradeQuantities.contains(item->GetEntry())))
                return false;
            actual.emplace(item->GetGUID().GetRawValue(), item->GetCount());
        }
    return !watch.offered.empty() && actual == std::map<uint64_t, uint32_t>(watch.offered.begin(), watch.offered.end());
}
}  // namespace

std::vector<InventoryItem> InventorySnapshot(PlayerbotAI* ai)
{
    std::vector<InventoryItem> result;
    if (!ai)
        return result;
    for (auto item : InventoryReader(ai).Read())
        result.push_back({item->GetGUID().GetRawValue(), item->GetEntry(), item->GetCount(), item->GetPos(),
                          item->GetItemRandomPropertyId(), item->GetTemplate()->Name1, item->IsEquipped(),
                          item->CanBeTraded()});
    return result;
}

std::vector<InventoryItem> SupplyOptions(PlayerbotAI* ai)
{
    std::vector<InventoryItem> result;
    if (!ai)
        return result;
    std::set<uint32_t> entries;
    for (auto item : InventoryReader(ai).Read())
    {
        auto proto = item->GetTemplate();
        // Small common consumables only; retain at least one for the companion.
        if (result.size() >= 6 || !item->CanBeTraded() || item->IsEquipped() || item->IsInTrade() ||
            item->GetCount() < 2 || proto->Class != ITEM_CLASS_CONSUMABLE ||
            (proto->SubClass != ITEM_SUBCLASS_FOOD && proto->SubClass != ITEM_SUBCLASS_BANDAGE) ||
            proto->Quality > ITEM_QUALITY_NORMAL || !entries.insert(item->GetEntry()).second)
            continue;
        result.push_back({item->GetGUID().GetRawValue(), item->GetEntry(), item->GetCount(), item->GetPos(),
                          item->GetItemRandomPropertyId(), proto->Name1, false, true});
    }
    return result;
}

Outcome StartInventory(Player* bot, PlayerbotAI* ai, InventoryExecution& execution, uint64_t nowMs)
{
    auto const& request = execution.request;
    auto result = [&](Status status, std::string detail)
    { return Outcome{request.id, status, std::move(detail), nowMs}; };
    if (request.kind == Kind::Inspect)
    {
        auto master = ai->GetMaster();
        if (!master || !master->GetSession())
            return result(Status::Failed, "inventory_requester_lost");
        std::map<std::string, uint32_t> supplies;
        for (auto const& item : InventorySnapshot(ai))
            supplies[item.name] += item.count;
        ChatHandler chat(master->GetSession());
        chat.SendSysMessage((bot->GetName() + " — current carried inventory:").c_str());
        for (auto const& [name, count] : supplies)
            chat.SendSysMessage((std::to_string(count) + " × " + name).c_str());
        return result(Status::Completed, "current_inventory_report_delivered");
    }
    if (request.items.empty() || !request.quantity)
        return result(Status::Failed, "missing_exact_inventory_reference");
    std::vector<Item*> items;
    for (auto const& observed : request.items)
    {
        auto item = BoundItem(bot, observed);
        if (!item || (request.tradeQuantities.empty() ? item->GetEntry() != request.objectId
                                                      : !request.tradeQuantities.contains(item->GetEntry())))
            return result(Status::Failed, "bound_inventory_changed");
        items.push_back(item);
    }
    auto item = items.front();
    if (request.kind == Kind::Equip)
    {
        if (request.quantity != 1 || items.size() != 1 || item->IsEquipped())
            return result(Status::Failed, "equip_requires_one_unequipped_item");
        auto guid = item->GetGUID();
        WorldPacket raw(CMSG_AUTOEQUIP_ITEM);
        raw << uint8(item->GetBagSlot()) << uint8(item->GetSlot());
        WorldPackets::Item::AutoEquipItem packet(std::move(raw));
        packet.Read();
        bot->GetSession()->HandleAutoEquipItemOpcode(packet);
        auto equipped = bot->GetItemByGuid(guid);
        return result(equipped && equipped->IsEquipped() ? Status::Completed : Status::Failed,
                      equipped && equipped->IsEquipped() ? "native_equipped_item_observed" : "native_equip_refused");
    }
    if (request.kind == Kind::Use)
    {
        if (request.quantity != 1 || items.size() != 1 || bot->IsNonMeleeSpellCast(false))
            return result(Status::Failed, "item_use_unavailable");
        auto target = request.target.guid ? ObjectAccessor::GetUnit(*bot, ObjectGuid(request.target.guid)) : bot;
        if (!target || (request.target.guid && !PlayerbotDialogueBridge::Matches(target, request.target)) ||
            !bot->IsInMap(target) || !bot->InSamePhase(target) || !bot->CanSeeOrDetect(target))
            return result(Status::Failed, "bound_item_target_lost");
        uint32 spell = 0;
        for (auto const& ability : item->GetTemplate()->Spells)
            if (ability.SpellId > 0 && ability.SpellTrigger == ITEM_SPELLTRIGGER_ON_USE)
            {
                spell = ability.SpellId;
                break;
            }
        if (!spell || !ai->CanCastSpell(spell, target, false, nullptr, item))
            return result(Status::Failed, "native_item_use_unavailable");
        {
            std::lock_guard lock(WatchMutex);
            auto& watch = Watches[bot->GetGUID().GetRawValue()];
            watch = Watch{request, {}};
            watch.item = item->GetGUID().GetRawValue();
            watch.spell = spell;
        }
        bot->StopMoving();
        WorldPacket packet(CMSG_USE_ITEM);
        packet << uint8(item->GetBagSlot()) << uint8(item->GetSlot()) << uint8(1) << spell << item->GetGUID()
               << uint32(0) << uint8(0) << uint32(TARGET_FLAG_UNIT) << target->GetPackGUID();
        bot->GetSession()->HandleUseItemOpcode(packet);
        return result(Status::Started, "awaiting_native_item_cast");
    }
    if (request.kind != Kind::Trade)
        return result(Status::Failed, "unsupported_inventory_operation");
    auto target = ObjectAccessor::GetPlayer(*bot, ObjectGuid(request.target.guid));
    if (!PlayerbotDialogueBridge::Matches(target, request.target) || !target->GetSession() ||
        target->GetSession()->IsBot() || target != ai->GetMaster() || !bot->IsInMap(target) ||
        !bot->InSamePhase(target) || !bot->IsWithinDistInMap(target, TRADE_DISTANCE, false) || !bot->IsAlive() ||
        !target->IsAlive() || bot->GetTradeData() || target->GetTradeData())
        return result(Status::Failed, "native_trade_partner_unavailable");
    std::map<uint32_t, uint32_t> requested = request.tradeQuantities;
    if (requested.empty())
        requested.emplace(request.objectId, request.quantity);
    uint64_t total = 0;
    for (auto const& [entry, count] : requested)
    {
        if (!entry || !count)
            return result(Status::Failed, "missing_exact_inventory_reference");
        total += count;
        uint64_t available = 0;
        for (auto stack : items)
            if (stack->GetEntry() == entry)
                available += stack->GetCount();
        if (available < count)
            return result(Status::Failed, "exact_quantity_unavailable_or_too_many_trade_stacks");
    }
    if (total != request.quantity || items.size() > TRADE_SLOT_TRADED_COUNT)
        return result(Status::Failed, "exact_quantity_unavailable_or_too_many_trade_stacks");
    std::set<uint64_t> unique;
    for (auto stack : items)
        if (!stack->CanBeTraded() || stack->IsEquipped() || !unique.insert(stack->GetGUID().GetRawValue()).second)
            return result(Status::Failed, "bound_item_not_tradable");
    std::vector<std::pair<uint64_t, uint32_t>> offered;
    for (auto const& [entry, quantity] : requested)
    {
        uint32 remaining = quantity;
        for (auto stack : items)
        {
            if (!remaining || stack->GetEntry() != entry)
                continue;
            auto count = std::min(remaining, stack->GetCount());
            if (count < stack->GetCount())
            {
                auto position = EmptyPosition(bot, stack, count);
                if (!position)
                    return result(Status::Failed, "free_bag_slot_needed_for_exact_quantity");
                bot->SplitItem(stack->GetPos(), position, count);
                stack = bot->GetItemByPos(position);
                if (!stack || stack->GetEntry() != entry || stack->GetCount() != count)
                    return result(Status::Failed, "native_stack_split_refused");
            }
            offered.emplace_back(stack->GetGUID().GetRawValue(), count);
            remaining -= count;
        }
    }
    {
        std::lock_guard lock(WatchMutex);
        Watches[bot->GetGUID().GetRawValue()] = Watch{request, offered};
    }
    WorldPacket initiate(CMSG_INITIATE_TRADE);
    initiate << target->GetGUID();
    bot->GetSession()->HandleInitiateTradeOpcode(initiate);
    if (!bot->GetTradeData() || bot->GetTrader() != target)
        return result(Status::Failed, "native_trade_initiation_refused");
    for (uint8 slot = 0; slot < offered.size(); ++slot)
    {
        auto stack = bot->GetItemByGuid(ObjectGuid(offered[slot].first));
        WorldPacket packet(CMSG_SET_TRADE_ITEM);
        packet << slot << uint8(stack->GetBagSlot()) << uint8(stack->GetSlot());
        bot->GetSession()->HandleSetTradeItemOpcode(packet);
    }
    if (!AllowManagedTrade(bot))
        return result(Status::Failed, "native_exact_trade_offer_refused");
    return result(Status::Started, "awaiting_native_trade_consent");
}

Outcome PollInventory(Player* bot, InventoryExecution const& execution, uint64_t nowMs)
{
    auto const& request = execution.request;
    auto result = [&](Status status, std::string detail)
    { return Outcome{request.id, status, std::move(detail), nowMs}; };
    Watch watch;
    {
        std::lock_guard lock(WatchMutex);
        auto found = Watches.find(bot->GetGUID().GetRawValue());
        if (found == Watches.end() || found->second.request.id != request.id)
            return result(Status::Unresolved, "inventory_observation_lost");
        watch = found->second;
    }
    if (request.kind == Kind::Use && watch.used)
        return result(Status::Completed, "native_item_spell_released:spell=" + std::to_string(watch.spell));
    if (watch.settled)
        return result(Status::Completed, "native_exact_trade_completed:entry=" + std::to_string(request.objectId) +
                                             ",quantity=" + std::to_string(request.quantity));
    if (watch.closed)
        return result(Status::Cancelled, "native_trade_declined_or_cancelled");
    if (request.kind == Kind::Trade && !AllowManagedTrade(bot))
        return result(Status::Failed, "bound_trade_offer_changed");
    if (request.kind == Kind::Trade && watch.refreshOffer)
    {
        // OPEN_WINDOW resets the human client's trade display. The initial
        // offer precedes that packet; resend it on the next map tick, after
        // both sessions have received OPEN_WINDOW, without changing consent.
        bot->GetTrader()->GetSession()->SendUpdateTrade(true);
        std::lock_guard lock(WatchMutex);
        auto found = Watches.find(bot->GetGUID().GetRawValue());
        if (found != Watches.end() && found->second.request.id == request.id)
            found->second.refreshOffer = false;
    }
    if (request.kind == Kind::Trade && bot->GetTradeData() && bot->GetTradeData()->GetTraderData()->IsAccepted())
    {
        // A ready human acceptance must not wait behind unrelated native AI
        // delays. Reuse the normal security/value/discount policy unchanged.
        WorldPacket packet(SMSG_TRADE_STATUS);
        packet << uint32(TRADE_STATUS_TRADE_ACCEPT);
        if (!TradeStatusAction(GET_PLAYERBOT_AI(bot)).Execute(Event("trade status", packet)))
            return result(Status::Failed, "native_trade_policy_or_settlement_refused");
        return result(Status::Pending, "awaiting_native_trade_settlement");
    }
    if (nowMs >= request.deadlineMs || (request.kind == Kind::Use && nowMs > execution.startedMs + 30000))
        return result(Status::Failed, "native_inventory_outcome_not_observed");
    return result(Status::Pending, "awaiting_native_inventory_outcome");
}

void CancelInventory(Player* bot, Request const& request)
{
    Watch watch;
    {
        std::lock_guard lock(WatchMutex);
        auto found = Watches.find(bot->GetGUID().GetRawValue());
        if (found == Watches.end() || found->second.request.id != request.id)
            return;
        watch = found->second;
        Watches.erase(found);
    }
    if (request.kind == Kind::Trade && bot->GetTrader() &&
        bot->GetTrader()->GetGUID().GetRawValue() == request.target.guid)
        bot->TradeCancel(true);
    if (request.kind == Kind::Use && !watch.used)
        if (auto casting = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            casting && casting->m_spellInfo->Id == watch.spell)
            bot->InterruptNonMeleeSpells(false, watch.spell);
}

bool AllowManagedTrade(Player* bot)
{
    std::lock_guard lock(WatchMutex);
    auto found = Watches.find(bot->GetGUID().GetRawValue());
    return found == Watches.end() || found->second.request.kind != Kind::Trade || ExactOffer(bot, found->second);
}

void ObserveInventoryPacket(Player* receiver, WorldPacket const* packet)
{
    if (!receiver || !packet || !PlayerbotDialogueBridge::Enabled() ||
        (packet->GetOpcode() != SMSG_SPELL_GO && packet->GetOpcode() != SMSG_TRADE_STATUS))
        return;
    std::lock_guard lock(WatchMutex);
    auto found = Watches.find(receiver->GetGUID().GetRawValue());
    if (found == Watches.end())
        return;
    auto& watch = found->second;
    try
    {
        WorldPacket data(*packet);
        data.rpos(0);
        if (packet->GetOpcode() == SMSG_TRADE_STATUS && watch.request.kind == Kind::Trade)
        {
            uint32 status;
            data >> status;
            if (status == TRADE_STATUS_OPEN_WINDOW)
                watch.refreshOffer = true;
            // Core emits COMPLETE only after both native acceptances and the inventory transfer.
            watch.settled = watch.settled || status == TRADE_STATUS_TRADE_COMPLETE;
            watch.closed = watch.closed || status == TRADE_STATUS_TRADE_CANCELED || status == TRADE_STATUS_CLOSE_WINDOW;
        }
        else if (packet->GetOpcode() == SMSG_SPELL_GO && watch.request.kind == Kind::Use)
        {
            ObjectGuid item, caster;
            uint8 count;
            uint32 spell;
            data >> item.ReadAsPacked() >> caster.ReadAsPacked() >> count >> spell;
            if (item.GetRawValue() == watch.item && caster == receiver->GetGUID() && spell == watch.spell)
                watch.used = true;
        }
    }
    catch (ByteBufferException const&)
    {
        // A malformed observation never proves completion.
    }
}
}  // namespace PlayerbotDialogue
