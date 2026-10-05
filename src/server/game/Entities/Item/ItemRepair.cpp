// PBC Character System integration changes, 2026-09-30; upstream notices preserved.
/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

// Living Azeroth: shared with Player::DurabilityRepair; never mutates the item.
#include "ItemRepair.h"
#include "DBCStores.h"
#include "Item.h"
#include "Log.h"
#include "World.h"

std::optional<uint32> CalculateItemRepairCost(Item const* item, float discount)
{
    if (!item)
        return 0;
    uint32 maximum = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
    uint32 current = item->GetUInt32Value(ITEM_FIELD_DURABILITY);
    if (!maximum || current >= maximum)
        return 0;
    auto proto = item->GetTemplate();
    auto costs = sDurabilityCostsStore.LookupEntry(proto->ItemLevel);
    auto quality = sDurabilityQualityStore.LookupEntry((proto->Quality + 1) * 2);
    if (!costs || !quality)
    {
        LOG_ERROR("entities.player", "RepairDurability: missing native price data for item {}", proto->ItemId);
        return std::nullopt;
    }
    uint32 multiplier = costs->multiplier[ItemSubClassToDurabilityMultiplierId(proto->Class, proto->SubClass)];
    uint32 price = uint32((maximum - current) * multiplier * double(quality->quality_mod));
    price = uint32(price * discount * sWorld->getRate(RATE_REPAIRCOST));
    return price ? price : 1; // Preserve the native minimum for damaged artifact-quality items.
}
