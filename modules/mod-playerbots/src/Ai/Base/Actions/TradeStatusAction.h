/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_TRADESTATUSACTION_H
#define PLAYERBOTS_TRADESTATUSACTION_H

#include <map>

#include "QueryItemUsageAction.h"
#include "TradeShares.h"

class Player;
class PlayerbotAI;

class TradeStatusAction : public QueryItemUsageAction
{
public:
    TradeStatusAction(PlayerbotAI* botAI) : QueryItemUsageAction(botAI, "accept trade") {}

    bool Execute(Event event) override;

    // Living Azeroth: the copper this bot wants on the trader's side before it accepts the trade they have open, as
    // the check on accepting works it out (0 when it wants nothing, or does not sell). For the trade window.
    uint32 AskingPrice();

private:
    // What each side of the trade is worth to a selling bot, in copper: the bot's items (less what is the trader's
    // to take back free) and gold, and the trader's items and gold.
    struct Worth
    {
        int32 botItems = 0;
        int32 bot = 0;
        int32 traderItems = 0;
        int32 trader = 0;
    };
    Worth TradeWorth(Player* trader, PlayerbotTradeShares::Free const& free);
    // The gold the trader puts in beyond what the bot asks for its side: a gift, shared back like a given item.
    uint64 GiftGold(Player* trader);

    void BeginTrade();
    void CancelTrade();
    bool CheckTrade();
    int32 CalculateCost(Player* player, bool sell);
    int32 SharedValue(std::map<uint32, uint32> const& items);
};

#endif
