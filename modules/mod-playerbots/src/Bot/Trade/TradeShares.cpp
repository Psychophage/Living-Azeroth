/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: trading by what was shared while grouped (see TradeShares.h).
#include "TradeShares.h"

#include <algorithm>

#include "Bag.h"
#include "Group.h"
#include "Item.h"
#include "Log.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "PlayerbotsDatabase.h"
#include "RandomPlayerbotMgr.h"
#include "TradeData.h"
#include "WorldPacket.h"
#include "WorldSession.h"

using TradeShares::Ledger;

namespace
{
constexpr uint32 GoldEntry = 0;  // the row that holds a ledger's gold

// Every stack in the bot's bags (not what it wears, nor its bank), with its bag and slot as the client names them.
template <typename Visit>
void ForEachStack(Player* bot, Visit visit)
{
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            visit(item, uint8(INVENTORY_SLOT_BAG_0), slot);
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint8 slot = 0; slot < bag->GetBagSize(); ++slot)
                if (Item* item = bag->GetItemByPos(slot))
                    visit(item, bagSlot, slot);
}

bool InBags(uint8 bag, uint8 slot)
{
    return (bag == INVENTORY_SLOT_BAG_0 && slot >= INVENTORY_SLOT_ITEM_START && slot < INVENTORY_SLOT_ITEM_END) ||
           (bag >= INVENTORY_SLOT_BAG_START && bag < INVENTORY_SLOT_BAG_END);
}

// An empty bag slot that takes `count` of the item, for splitting a stack.
uint16 EmptyPosition(Player* bot, Item* item, uint32 count)
{
    uint16 found = 0;
    auto fits = [&](uint8 bag, uint8 slot)
    {
        ItemPosCountVec destinations;
        return !bot->GetItemByPos(bag, slot) &&
               bot->CanStoreNewItem(bag, slot, destinations, item->GetEntry(), count) == EQUIP_ERR_OK;
    };
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END && !found; ++slot)
        if (fits(INVENTORY_SLOT_BAG_0, slot))
            found = uint16(INVENTORY_SLOT_BAG_0 << 8) | slot;
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END && !found; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint8 slot = 0; slot < bag->GetBagSize() && !found; ++slot)
                if (fits(bagSlot, slot))
                    found = uint16(bagSlot << 8) | slot;
    return found;
}

// What the bot has put in the trade, by entry.
std::map<uint32, uint32> InTrade(Player* bot)
{
    std::map<uint32, uint32> items;
    if (TradeData* trade = bot->GetTradeData())
        for (uint8 slot = 0; slot < TRADE_SLOT_TRADED_COUNT; ++slot)
            if (Item* item = trade->GetItem(TradeSlots(slot)))
                items[item->GetEntry()] += item->GetCount();
    return items;
}

// A bot that sells what it has: a random bot. Someone's own character played as a bot keeps what is theirs.
bool Sells(Player* bot)
{
    return sPlayerbotAIConfig.IsInRandomAccountList(bot->GetSession()->GetAccountId());
}
}  // namespace

PlayerbotTradeShares& PlayerbotTradeShares::Instance()
{
    static PlayerbotTradeShares instance;
    return instance;
}

void PlayerbotTradeShares::Configure()
{
    _gmLevel = sPlayerbotAIConfig.tradeSharesGmLevel;
    _enabled.store(sPlayerbotAIConfig.tradeSharesEnabled);
}

void PlayerbotTradeShares::Initialize()
{
    if (!Enabled())
        return;
    std::lock_guard lock(_mutex);
    _ledgers.clear();
    if (auto rows = PlayerbotsDatabase.Query(PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_SEL_TRADE_SHARES)))
        do
        {
            auto f = rows->Fetch();
            Ledger& ledger = _ledgers[{f[0].Get<uint32>(), f[1].Get<uint32>()}];
            uint32 entry = f[2].Get<uint32>();
            uint64 amount = f[3].Get<uint64>();
            if (entry == GoldEntry)
                ledger.gold = amount;
            else
                ledger.items[entry] = uint32(std::min<uint64>(amount, UINT32_MAX));
        } while (rows->NextRow());
    LOG_INFO("playerbots", "Trade shares loaded: {} bot and player pairs", _ledgers.size());
}

std::vector<uint32> PlayerbotTradeShares::PlayersGroupedWith(Player* bot) const
{
    std::vector<uint32> players;
    if (Group* group = bot->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource(); member && member != bot && IsRealPlayer(member))
                players.push_back(member->GetGUID().GetCounter());
    return players;
}

Ledger PlayerbotTradeShares::LedgerFor(uint32 bot, uint32 player) const
{
    std::lock_guard lock(_mutex);
    auto found = _ledgers.find({bot, player});
    return found == _ledgers.end() ? Ledger{} : found->second;
}

// Writes the rows that changed. Called with the lock held.
void PlayerbotTradeShares::Save(Key const& key, Ledger const& before, Ledger const& after)
{
    auto write = [&](uint32 entry, uint64 was, uint64 now)
    {
        if (was == now)
            return;
        if (now)
        {
            auto stmt = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_REP_TRADE_SHARE);
            stmt->SetData(0, key.first);
            stmt->SetData(1, key.second);
            stmt->SetData(2, entry);
            stmt->SetData(3, now);
            PlayerbotsDatabase.Execute(stmt);
        }
        else
        {
            auto stmt = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_DEL_TRADE_SHARE);
            stmt->SetData(0, key.first);
            stmt->SetData(1, key.second);
            stmt->SetData(2, entry);
            PlayerbotsDatabase.Execute(stmt);
        }
    };
    write(GoldEntry, before.gold, after.gold);
    std::set<uint32> entries;
    for (auto const& [entry, count] : before.items)
        entries.insert(entry);
    for (auto const& [entry, count] : after.items)
        entries.insert(entry);
    for (uint32 entry : entries)
    {
        auto was = before.items.find(entry);
        auto now = after.items.find(entry);
        write(entry, was == before.items.end() ? 0 : was->second, now == after.items.end() ? 0 : now->second);
    }
}

void PlayerbotTradeShares::OnLoot(Player* bot, uint32 entry, uint32 count)
{
    if (!Enabled() || !bot || !GET_PLAYERBOT_AI(bot) || IsSelfBot(bot))
        return;
    std::vector<uint32> players = PlayersGroupedWith(bot);
    std::lock_guard lock(_mutex);
    for (uint32 player : players)
    {
        Key key{bot->GetGUID().GetCounter(), player};
        Ledger& ledger = _ledgers[key];
        Ledger before = ledger;
        TradeShares::AddItems(ledger, entry, count);
        Save(key, before, ledger);
    }
}

void PlayerbotTradeShares::OnMoney(Player* bot, int32 amount)
{
    // Gold spent is not taken off: the allowance is capped by what the bot still has when it is asked for.
    // The game moves a trade's gold while the trade is still open; OnTraded counts the part that was a gift.
    if (!Enabled() || amount <= 0 || !bot || !GET_PLAYERBOT_AI(bot) || IsSelfBot(bot) || bot->GetTradeData())
        return;
    std::vector<uint32> players = PlayersGroupedWith(bot);
    std::lock_guard lock(_mutex);
    for (uint32 player : players)
    {
        Key key{bot->GetGUID().GetCounter(), player};
        Ledger& ledger = _ledgers[key];
        Ledger before = ledger;
        TradeShares::AddGold(ledger, uint64(amount));
        Save(key, before, ledger);
    }
}

void PlayerbotTradeShares::OnTraded(Player* bot, Player* trader, std::map<uint32, uint32> const& given,
                                    std::map<uint32, uint32> const& taken, uint64 gold, uint64 giftGold)
{
    if (!Enabled() || !bot || !trader || !IsRealPlayer(trader))
        return;
    std::map<uint32, uint32> owned = Owned(bot);
    std::lock_guard lock(_mutex);
    Key key{bot->GetGUID().GetCounter(), trader->GetGUID().GetCounter()};
    Ledger& ledger = _ledgers[key];
    Ledger before = ledger;
    TradeShares::Take(ledger, taken, gold);
    for (auto const& [entry, count] : given)
        TradeShares::AddItems(ledger, entry, count);
    TradeShares::AddGold(ledger, giftGold);
    // Whatever shared the bot no longer has (used, sold, lost) is forgotten.
    for (auto it = ledger.items.begin(); it != ledger.items.end();)
    {
        auto found = owned.find(it->first);
        uint32 has = found == owned.end() ? 0 : found->second;
        auto next = std::next(it);
        TradeShares::Trim(ledger, it->first, has);
        it = next;
    }
    Save(key, before, ledger);
    if (ledger.Empty())
        _ledgers.erase(key);
}

std::map<uint32, uint32> PlayerbotTradeShares::Owned(Player* bot) const
{
    std::map<uint32, uint32> owned;
    ForEachStack(bot, [&](Item* item, uint8, uint8) { owned[item->GetEntry()] += item->GetCount(); });
    return owned;
}

// A player's own character: on their account, or on an account linked to theirs and brought in by them (Playerbots'
// `account link`, then `bot add`, makes them its master). A random bot that joins a group gets a master too, so a
// random bot is never one.
bool PlayerbotTradeShares::FullAccess(Player* bot, Player* trader) const
{
    if (!bot || !trader || !bot->GetSession() || !trader->GetSession())
        return false;
    if (TradesFreely(trader))
        return true;
    if (Sells(bot))
        return false;
    PlayerbotAI* ai = GET_PLAYERBOT_AI(bot);
    return bot->GetSession()->GetAccountId() == trader->GetSession()->GetAccountId() ||
           (ai && ai->GetMaster() == trader);
}

PlayerbotTradeShares::Free PlayerbotTradeShares::FreeInTrade(Player* bot, Player* trader) const
{
    Free free;
    TradeData* trade = bot->GetTradeData();
    if (!trade)
        return free;
    std::map<uint32, uint32> offered = InTrade(bot);
    if (FullAccess(bot, trader))
    {
        free.items = offered;
        free.gold = trade->GetMoney();
        free.all = true;
        return free;
    }
    Ledger ledger = LedgerFor(bot->GetGUID().GetCounter(), trader->GetGUID().GetCounter());
    std::map<uint32, uint32> owned = Owned(bot);
    free.all = true;
    for (auto const& [entry, count] : offered)
    {
        // Items in the trade stay in the bot's bags until it goes through, so they count as owned.
        uint32 shared = TradeShares::SharedCount(ledger, entry, owned[entry]);
        if (shared)
            free.items[entry] = std::min(shared, count);
        if (shared < count)
            free.all = false;
    }
    uint64 allowance = TradeShares::GoldAllowance(ledger, bot->GetMoney());
    free.gold = std::min<uint64>(allowance, trade->GetMoney());
    if (trade->GetMoney() > allowance)
        free.all = false;
    return free;
}

PlayerbotTradeShares::View PlayerbotTradeShares::ViewFor(Player* bot, Player* trader) const
{
    View view;
    view.money = bot->GetMoney();
    view.full = FullAccess(bot, trader);
    view.sells = !view.full && Sells(bot);
    std::map<uint32, uint32> owned = Owned(bot);
    Ledger ledger = LedgerFor(bot->GetGUID().GetCounter(), trader->GetGUID().GetCounter());
    if (!view.full)
        for (auto const& [entry, count] : owned)
            if (uint32 shared = TradeShares::SharedCount(ledger, entry, count))
                view.shared[entry] = shared;
    view.allowance = view.full ? view.money : TradeShares::GoldAllowance(ledger, view.money);
    double multiplier = view.sells ? sRandomPlayerbotMgr.GetSellMultiplier(bot) : 0.0;
    // (Item::IsInTrade is only set while a trade is being carried out; the trade itself knows what it holds.)
    TradeData* trade = bot->GetTradeData();
    ForEachStack(bot, [&](Item* item, uint8 bag, uint8 slot)
    {
        Stack stack;
        stack.bag = bag;
        stack.slot = slot;
        stack.entry = item->GetEntry();
        stack.count = item->GetCount();
        stack.property = item->GetItemRandomPropertyId();
        stack.tradable = item->CanBeTraded();
        stack.inTrade = trade && trade->HasItem(item->GetGUID());
        if (view.sells && !view.shared.count(stack.entry))
            stack.price = uint32(item->GetTemplate()->SellPrice * stack.count * multiplier);
        view.stacks.push_back(stack);
    });
    return view;
}

std::string PlayerbotTradeShares::Open(Player* bot, Player* trader) const
{
    if (!Enabled())
        return "disabled";
    if (!bot->GetTradeData() || bot->GetTrader() != trader)
        return "no_trade";
    return "";
}

std::string PlayerbotTradeShares::Offer(Player* bot, Player* trader, uint8 bag, uint8 slot, uint32 count)
{
    if (std::string error = Open(bot, trader); !error.empty())
        return error;
    Item* item = InBags(bag, slot) ? bot->GetItemByPos(bag, slot) : nullptr;
    if (!item)
        return "no_item";
    TradeData* trade = bot->GetTradeData();
    if (trade->HasItem(item->GetGUID()))
        return "in_trade";
    if (!item->CanBeTraded())
        return "bound";
    uint32 const entry = item->GetEntry();
    uint32 want = count ? std::min(count, item->GetCount()) : item->GetCount();
    if (!FullAccess(bot, trader))
    {
        Ledger ledger = LedgerFor(bot->GetGUID().GetCounter(), trader->GetGUID().GetCounter());
        uint32 shared = TradeShares::SharedCount(ledger, entry, Owned(bot)[entry]);
        uint32 offered = InTrade(bot)[entry];
        uint32 free = shared > offered ? shared - offered : 0;
        if (!free)
        {
            if (!Sells(bot))
                return "not_yours";
            if (!item->GetTemplate()->SellPrice)
                return "not_for_sale";
        }
        else if (!count || !Sells(bot))
            want = std::min(want, free); // only the shared part, unless the player asked a seller for more
    }
    if (want < item->GetCount())
    {
        // Part of the stack goes into the trade on its own.
        uint16 position = EmptyPosition(bot, item, want);
        if (!position)
            return "no_room";
        bot->SplitItem(item->GetPos(), position, want);
        item = bot->GetItemByPos(position);
        if (!item || item->GetEntry() != entry || item->GetCount() != want)
            return "split_failed";
    }
    int8 tradeSlot = -1;
    for (uint8 i = 0; i < TRADE_SLOT_TRADED_COUNT && tradeSlot < 0; ++i)
        if (!trade->GetItem(TradeSlots(i)))
            tradeSlot = int8(i);
    if (tradeSlot < 0)
        return "trade_full";
    WorldPacket packet(CMSG_SET_TRADE_ITEM, 3);
    packet << uint8(tradeSlot) << uint8(item->GetBagSlot()) << uint8(item->GetSlot());
    bot->GetSession()->HandleSetTradeItemOpcode(packet);
    return trade->HasItem(item->GetGUID()) ? "" : "refused";
}

std::string PlayerbotTradeShares::OfferGold(Player* bot, Player* trader, uint64 copper)
{
    if (std::string error = Open(bot, trader); !error.empty())
        return error;
    if (copper > bot->GetMoney())
        return "too_much";
    if (!FullAccess(bot, trader))
    {
        Ledger ledger = LedgerFor(bot->GetGUID().GetCounter(), trader->GetGUID().GetCounter());
        if (copper > TradeShares::GoldAllowance(ledger, bot->GetMoney()))
            return "too_much";
    }
    WorldPacket packet(CMSG_SET_TRADE_GOLD, 4);
    packet << uint32(copper);
    bot->GetSession()->HandleSetTradeGoldOpcode(packet);
    return bot->GetTradeData() && bot->GetTradeData()->GetMoney() == copper ? "" : "refused";
}

std::string PlayerbotTradeShares::TakeBack(Player* bot, Player* trader, uint8 tradeSlot)
{
    if (std::string error = Open(bot, trader); !error.empty())
        return error;
    if (tradeSlot >= TRADE_SLOT_TRADED_COUNT || !bot->GetTradeData()->GetItem(TradeSlots(tradeSlot)))
        return "no_item";
    WorldPacket packet(CMSG_CLEAR_TRADE_ITEM, 1);
    packet << tradeSlot;
    bot->GetSession()->HandleClearTradeItemOpcode(packet);
    return "";
}

bool PlayerbotTradeShares::MayTradeFreely(Player* player) const
{
    return Enabled() && player && player->GetSession() && IsRealPlayer(player) &&
           uint32(player->GetSession()->GetSecurity()) >= _gmLevel;
}

bool PlayerbotTradeShares::TradesFreely(Player* player) const
{
    if (!MayTradeFreely(player))
        return false;
    std::lock_guard lock(_mutex);
    return _tradingFreely.count(player->GetGUID().GetCounter()) != 0;
}

bool PlayerbotTradeShares::SetTradeFreely(Player* player, bool on)
{
    if (!MayTradeFreely(player))
        return false;
    std::lock_guard lock(_mutex);
    if (on)
        _tradingFreely.insert(player->GetGUID().GetCounter());
    else
        _tradingFreely.erase(player->GetGUID().GetCounter());
    return true;
}

void PlayerbotTradeShares::Forget(Player* player)
{
    if (!player)
        return;
    std::lock_guard lock(_mutex);
    _tradingFreely.erase(player->GetGUID().GetCounter());
}
