/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: the sharing rules, on plain values.
#include "TradeSharesLedger.h"

#include <algorithm>
#include <limits>

namespace TradeShares
{
void AddItems(Ledger& ledger, uint32_t entry, uint32_t count)
{
    if (!entry || !count)
        return;
    uint32_t& held = ledger.items[entry];
    held = count > std::numeric_limits<uint32_t>::max() - held ? std::numeric_limits<uint32_t>::max() : held + count;
}

void AddGold(Ledger& ledger, uint64_t copper)
{
    ledger.gold = copper > std::numeric_limits<uint64_t>::max() - ledger.gold ? std::numeric_limits<uint64_t>::max()
                                                                             : ledger.gold + copper;
}

uint32_t SharedCount(Ledger const& ledger, uint32_t entry, uint32_t owned)
{
    auto found = ledger.items.find(entry);
    return found == ledger.items.end() ? 0 : std::min(found->second, owned);
}

uint64_t GoldAllowance(Ledger const& ledger, uint64_t money)
{
    return std::min(ledger.gold, money);
}

bool Covers(Ledger const& ledger, std::map<uint32_t, uint32_t> const& items, uint64_t gold, uint64_t money)
{
    if (gold > GoldAllowance(ledger, money))
        return false;
    for (auto const& [entry, count] : items)
    {
        auto found = ledger.items.find(entry);
        if (found == ledger.items.end() || found->second < count)
            return false;
    }
    return true;
}

void Take(Ledger& ledger, std::map<uint32_t, uint32_t> const& items, uint64_t gold)
{
    ledger.gold -= std::min(ledger.gold, gold);
    for (auto const& [entry, count] : items)
    {
        auto found = ledger.items.find(entry);
        if (found == ledger.items.end())
            continue;
        if (found->second <= count)
            ledger.items.erase(found);
        else
            found->second -= count;
    }
}

void Trim(Ledger& ledger, uint32_t entry, uint32_t owned)
{
    auto found = ledger.items.find(entry);
    if (found == ledger.items.end() || found->second <= owned)
        return;
    if (owned)
        found->second = owned;
    else
        ledger.items.erase(found);
}
}  // namespace TradeShares
