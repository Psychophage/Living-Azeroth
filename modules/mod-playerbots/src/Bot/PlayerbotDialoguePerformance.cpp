// SPDX-License-Identifier: GPL-2.0-or-later
// Living Azeroth changes; upstream authorship retained in AUTHORS.
#include "PlayerbotDialoguePerformance.h"

#include <algorithm>
#include <set>

#include "Bag.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "ItemPackets.h"
#include "Player.h"
#include "Playerbots.h"
#include "WorldSession.h"

namespace PlayerbotDialogue
{
namespace
{
constexpr char RestorationName[] = "PBC exact restoration";
constexpr char RestorationIcon[] = "pbc-restore-v1";

std::optional<uint32> RestorationSlot(Player* bot)
{
    for (auto const& [index, set] : bot->GetEquipmentSets())
        if (set.state != EQUIPMENT_SET_DELETED && set.Name == RestorationName && set.IconName == RestorationIcon)
            return index;
    return {};
}

bool SaveRestoration(Player* bot, PerformanceState const& state)
{
    auto index = RestorationSlot(bot);
    if (state.equipment.empty())
    {
        if (index)
            bot->DeleteEquipmentSet(bot->GetEquipmentSets().at(*index).Guid);
    }
    else
    {
        if (!index)
            for (uint32 candidate = 0; candidate < MAX_EQUIPMENT_SET_INDEX; ++candidate)
                if (!bot->GetEquipmentSets().contains(candidate))
                {
                    index = candidate;
                    break;
                }
        if (!index)
            return false;
        EquipmentSet set;
        set.Guid = bot->GetEquipmentSets().contains(*index) ? bot->GetEquipmentSets().at(*index).Guid : 0;
        set.Name = RestorationName;
        set.IconName = RestorationIcon;
        set.IgnoreMask = (1u << EQUIPMENT_SLOT_END) - 1;
        for (auto const& saved : state.equipment)
        {
            uint8 slot = uint8(saved.position);
            set.Items[slot] = ObjectGuid(saved.guid);
            set.IgnoreMask &= ~(1u << slot);
        }
        bot->SetEquipmentSet(*index, set);
    }
    if (auto ai = GET_PLAYERBOT_AI(bot))
    {
        // Invalidate disposition caches immediately, including the lists used by
        // automatic disenchanting/trade. Do not reset unrelated AI values or formation.
        auto context = ai->GetAiObjectContext();
        for (auto const& item : state.equipment)
        {
            context->GetValue<ItemUsage>("item usage", std::to_string(item.entry))->Reset();
            context->GetValue<ItemUsage>("item usage", std::to_string(item.entry) + "," + std::to_string(item.property))
                ->Reset();
        }
        for (unsigned usage = ITEM_USAGE_NONE; usage <= ITEM_USAGE_AMMO; ++usage)
            context->GetValue<std::vector<Item*>>("inventory items", "usage " + std::to_string(usage))->Reset();
    }
    // Existing native login loading and inventory persistence provide an atomic
    // old/new durable snapshot. Submission is asynchronous, not a durability acknowledgement.
    auto transaction = CharacterDatabase.BeginTransaction();
    bot->SaveInventoryAndGoldToDB(transaction);
    CharacterDatabase.CommitTransaction(transaction);
    return true;
}

uint16 EmptySlot(Player* bot, Item* item, std::set<uint16> const& reserved)
{
    auto fits = [&](uint8 bag, uint8 slot)
    {
        uint16 position = uint16(bag << 8) | slot;
        ItemPosCountVec destinations;
        return !reserved.contains(position) && !bot->GetItemByPos(position) &&
               bot->CanStoreItem(bag, slot, destinations, item) == EQUIP_ERR_OK;
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
}  // namespace

std::vector<InventoryItem> SavedPerformanceEquipment(Player* bot)
{
    std::vector<InventoryItem> result;
    if (!bot)
        return result;
    auto index = RestorationSlot(bot);
    if (!index)
        return result;
    auto const& set = bot->GetEquipmentSets().at(*index);
    for (uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
        if (!(set.IgnoreMask & (1u << slot)) && !set.Items[slot].IsEmpty())
        {
            InventoryItem saved;
            saved.guid = set.Items[slot].GetRawValue();
            saved.position = uint16(INVENTORY_SLOT_BAG_0 << 8) | slot;
            if (auto item = bot->GetItemByGuid(set.Items[slot]))
            {
                saved.entry = item->GetEntry();
                saved.count = item->GetCount();
                saved.name = item->GetTemplate()->Name1;
                saved.property = item->GetItemRandomPropertyId();
                saved.equipped = item->IsEquipped();
            }
            result.push_back(std::move(saved));
        }
    return result;
}

bool IsPerformanceEquipment(Player* bot, uint64_t itemGuid)
{
    auto saved = SavedPerformanceEquipment(bot);
    return std::any_of(saved.begin(), saved.end(),
                       [&](auto const& item)
                       {
                           if (item.guid == itemGuid)
                               return true;
                           auto owned = bot->GetItemByGuid(ObjectGuid(item.guid));
                           auto container = owned ? owned->GetContainer() : nullptr;
                           return container && container->GetGUID().GetRawValue() == itemGuid;
                       });
}

void StopPerformance(Player* bot, PerformanceState& state)
{
    if (state.danceEndsMs && bot->GetUInt32Value(UNIT_NPC_EMOTESTATE) == EMOTE_STATE_DANCE)
        bot->ClearEmoteState();
    state.danceEndsMs = 0;
}

bool UpdatePerformance(Player* bot, PerformanceState& state, uint64_t nowMs)
{
    if (!state.danceEndsMs)
        return false;
    if (bot->IsAlive() && bot->IsInWorld() && !bot->IsInCombat() && !bot->isMoving() && nowMs < state.danceEndsMs &&
        bot->GetUInt32Value(UNIT_NPC_EMOTESTATE) == EMOTE_STATE_DANCE)
        return false;
    StopPerformance(bot, state);
    return true;
}

Outcome Perform(Player* bot, Request const& request, PerformanceState& state, uint64_t nowMs)
{
    auto result = [&](Status status, std::string detail)
    { return Outcome{request.id, status, std::move(detail), nowMs}; };
    if (request.kind == Kind::StopDance)
    {
        StopPerformance(bot, state);
        return result(Status::Completed, "native_dance_stopped");
    }
    if (request.kind == Kind::Dance)
    {
        if (!bot->IsAlive() || bot->IsInCombat() || bot->IsMounted() || bot->GetTradeData())
            return result(Status::Failed, "performance_unavailable");
        bot->StopMoving();
        bot->SetEmoteState(EMOTE_STATE_DANCE);
        state.danceEndsMs = nowMs + (request.durationMs ? std::min(request.durationMs, 300000u) : 30000u);
        return result(Status::Completed, "native_dance_state_observed");
    }
    if (bot->IsInCombat() || !bot->IsAlive() || bot->GetTradeData())
        return result(Status::Failed, "equipment_change_unavailable");
    if (state.equipment.empty())
        state.equipment = SavedPerformanceEquipment(bot);
    if (request.kind == Kind::Unequip)
    {
        if (!RestorationSlot(bot) && bot->GetEquipmentSets().size() >= MAX_EQUIPMENT_SET_INDEX)
            return result(Status::Failed, "free_native_equipment_set_needed");
        if (request.items.empty())
            return result(Status::Failed, "no_bound_equipped_items");
        std::vector<std::pair<InventoryItem, uint16>> moves;
        std::set<uint16> reserved;
        std::set<uint64> bound;
        for (auto const& observed : request.items)
        {
            auto item = bot->GetItemByGuid(ObjectGuid(observed.guid));
            if (!bound.insert(observed.guid).second || !item || !item->IsEquipped() ||
                uint8(observed.position) >= EQUIPMENT_SLOT_END || item->GetPos() != observed.position ||
                item->GetEntry() != observed.entry || item->GetCount() != observed.count ||
                bot->CanUnequipItem(item->GetPos(), false) != EQUIP_ERR_OK)
                return result(Status::Failed, "bound_equipment_changed_or_cannot_remove");
            auto destination = EmptySlot(bot, item, reserved);
            if (!destination)
                return result(Status::Failed, "free_bag_space_needed_for_equipment");
            reserved.insert(destination);
            moves.emplace_back(observed, destination);
        }
        // Preserve the first original item for each slot. Changing that slot again
        // cannot silently replace its original restoration identity.
        for (auto const& [observed, destination] : moves)
            for (auto const& saved : state.equipment)
                if (saved.position == observed.position && saved.guid != observed.guid)
                    return result(Status::Failed, "restore_original_slot_before_removing_replacement");
        // Reserve distinct destinations for the entire outfit before changing any slot.
        for (auto const& [observed, destination] : moves)
        {
            WorldPacket raw(CMSG_SWAP_ITEM);
            raw << uint8(destination >> 8) << uint8(destination) << uint8(observed.position >> 8)
                << uint8(observed.position);
            WorldPackets::Item::SwapItem packet(std::move(raw));
            packet.Read();
            bot->GetSession()->HandleSwapItem(packet);
            auto item = bot->GetItemByGuid(ObjectGuid(observed.guid));
            if (!item || item->GetPos() != destination)
            {
                SaveRestoration(bot, state);
                return result(Status::Failed, "native_equipment_removal_incomplete");
            }
            if (std::none_of(state.equipment.begin(), state.equipment.end(),
                             [&](auto const& saved) { return saved.guid == observed.guid; }))
                state.equipment.push_back(observed);
        }
        if (!SaveRestoration(bot, state))
            return result(Status::Failed, "equipment_snapshot_could_not_be_saved");
        return result(Status::Completed, "native_equipment_removed_and_owned");
    }
    if (request.kind == Kind::RestoreEquipment)
    {
        if (state.equipment.empty())
            return result(Status::Failed, "no_saved_equipment_to_restore");
        for (auto const& saved : state.equipment)
        {
            auto item = bot->GetItemByGuid(ObjectGuid(saved.guid));
            uint16 destination = 0;
            if (!item || item->GetEntry() != saved.entry ||
                (item->GetPos() != saved.position &&
                 bot->CanEquipItem(uint8(saved.position), destination, item, true) != EQUIP_ERR_OK))
                return result(Status::Failed, "saved_equipment_missing_or_cannot_restore");
        }
        StopPerformance(bot, state);
        for (auto const& saved : state.equipment)
        {
            auto item = bot->GetItemByGuid(ObjectGuid(saved.guid));
            if (item->GetPos() == saved.position)
                continue;
            WorldPacket raw(CMSG_AUTOEQUIP_ITEM_SLOT);
            raw << item->GetGUID() << uint8(saved.position);
            WorldPackets::Item::AutoEquipItemSlot packet(std::move(raw));
            packet.Read();
            bot->GetSession()->HandleAutoEquipItemSlotOpcode(packet);
            if (item->GetPos() != saved.position)
            {
                SaveRestoration(bot, state);
                return result(Status::Failed, "native_equipment_restoration_incomplete");
            }
        }
        // Verify the complete result before releasing any identity or protection.
        for (auto const& saved : state.equipment)
        {
            auto item = bot->GetItemByPos(saved.position);
            if (!item || item->GetGUID().GetRawValue() != saved.guid)
            {
                SaveRestoration(bot, state);
                return result(Status::Failed, "native_equipment_restoration_incomplete");
            }
        }
        state.equipment.clear();
        SaveRestoration(bot, state);
        return result(Status::Completed, "native_original_equipment_restored");
    }
    return result(Status::Failed, "unsupported_performance_operation");
}
}  // namespace PlayerbotDialogue
