/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: what a bot hands over in a trade. A player's own characters played as bots hand over everything.
// Any other bot hands back, without payment, what it got while grouped with that player: its loot, the items the
// player gave it, and the gold it gained, at most what it still has. Everything else stays as Playerbots has it
// (random bots sell at a price; someone else's characters keep theirs). A GM may switch on trading freely with
// any bot. Opt-in: AiPlayerbot.TradeShares.Enabled.
#ifndef PLAYERBOTS_TRADE_SHARES_H
#define PLAYERBOTS_TRADE_SHARES_H

#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "Define.h"
#include "TradeSharesLedger.h"

class Player;

class PlayerbotTradeShares
{
public:
    // One stack in a bot's bags, as the player trading with it sees it.
    struct Stack
    {
        uint8 bag = 0;  // 255 for the backpack, else the bag's inventory slot
        uint8 slot = 0;
        uint32 entry = 0;
        uint32 count = 0;
        int32 property = 0;  // random property ("of the Bear")
        uint32 price = 0;    // what the bot asks for it, in copper; 0 when free or not for sale
        bool tradable = false;
        bool inTrade = false;
    };
    struct View
    {
        std::vector<Stack> stacks;
        std::map<uint32, uint32> shared;  // entry -> how many the bot hands over free
        uint64 money = 0;
        uint64 allowance = 0;  // the gold it hands over
        bool full = false;     // everything is the player's to take
        bool sells = false;    // the rest it sells (a random bot)
    };

    static PlayerbotTradeShares& Instance();
    void Configure();
    void Initialize();  // World thread, at startup.
    bool Enabled() const { return _enabled.load(); }

    // What a bot gets while grouped with players. Any map thread.
    void OnLoot(Player* bot, uint32 entry, uint32 count);
    void OnMoney(Player* bot, int32 amount);
    // A trade between a bot and a player went through: what the player gave and took, and the gold the bot gave.
    void OnTraded(Player* bot, Player* trader, std::map<uint32, uint32> const& given,
                  std::map<uint32, uint32> const& taken, uint64 gold);

    // Everything is the trader's to take: the bot is one of their own characters, or they are a GM trading freely.
    bool FullAccess(Player* bot, Player* trader) const;
    // What of the bot's side of the open trade is free for the trader: items (entry -> how many) and gold.
    struct Free
    {
        std::map<uint32, uint32> items;
        uint64 gold = 0;
        bool all = false;  // the bot's whole side
    };
    Free FreeInTrade(Player* bot, Player* trader) const;
    View ViewFor(Player* bot, Player* trader) const;

    // Into the trade the two have open; an empty string when done, else why not.
    std::string Offer(Player* bot, Player* trader, uint8 bag, uint8 slot);
    std::string OfferGold(Player* bot, Player* trader, uint64 copper);
    std::string TakeBack(Player* bot, Player* trader, uint8 tradeSlot);

    // GMs of the configured level and up may trade freely with any bot, while they have it switched on.
    bool MayTradeFreely(Player* player) const;
    bool TradesFreely(Player* player) const;
    bool SetTradeFreely(Player* player, bool on);
    void Forget(Player* player);

private:
    using Key = std::pair<uint32, uint32>;  // bot, player (GUID counters)

    TradeShares::Ledger LedgerFor(uint32 bot, uint32 player) const;
    void Save(Key const& key, TradeShares::Ledger const& before, TradeShares::Ledger const& after);
    std::vector<uint32> PlayersGroupedWith(Player* bot) const;
    std::map<uint32, uint32> Owned(Player* bot) const;
    std::string Open(Player* bot, Player* trader) const;

    std::atomic<bool> _enabled{false};
    uint32 _gmLevel = 2;
    mutable std::mutex _mutex;
    std::map<Key, TradeShares::Ledger> _ledgers;
    std::set<uint32> _tradingFreely;
};

#endif
