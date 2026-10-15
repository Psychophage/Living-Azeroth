/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: what one bot got while grouped with one player, and so will hand back to them without payment.
// Plain values, no game objects, so the rules can be tested on their own (tests/trade).
#ifndef PLAYERBOTS_TRADE_SHARES_LEDGER_H
#define PLAYERBOTS_TRADE_SHARES_LEDGER_H

#include <cstdint>
#include <map>

namespace TradeShares
{
struct Ledger
{
    std::map<uint32_t, uint32_t> items;  // item entry -> how many
    uint64_t gold = 0;                   // copper

    bool Empty() const { return items.empty() && !gold; }
};

void AddItems(Ledger& ledger, uint32_t entry, uint32_t count);
void AddGold(Ledger& ledger, uint64_t copper);

// How many of an entry are shared, at most what the bot still has; the rest may have been used, sold or lost.
uint32_t SharedCount(Ledger const& ledger, uint32_t entry, uint32_t owned);
// The gold the bot will hand over: what it gained, at most what it still has.
uint64_t GoldAllowance(Ledger const& ledger, uint64_t money);
// Whether the items (entry -> count) and gold a bot puts in a trade are all shared with the trader.
bool Covers(Ledger const& ledger, std::map<uint32_t, uint32_t> const& items, uint64_t gold, uint64_t money);
// After a trade: what the trader took is no longer shared.
void Take(Ledger& ledger, std::map<uint32_t, uint32_t> const& items, uint64_t gold);
// Shrinks an entry to what the bot still has, so a later find of the same item is not shared by mistake.
void Trim(Ledger& ledger, uint32_t entry, uint32_t owned);
}  // namespace TradeShares

#endif
