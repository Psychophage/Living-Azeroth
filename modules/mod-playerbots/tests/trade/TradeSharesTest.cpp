/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: the trade sharing rules, without a server.
#include <iostream>

#include "TradeSharesLedger.h"

using namespace TradeShares;

static unsigned checks = 0;
static unsigned failures = 0;

static void Check(bool condition, char const* message)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << "\n";
    }
}

int main()
{
    constexpr uint32_t Linen = 2589, Sword = 2488;

    // A bot that had 5 linen loots 3 more with the player: 3 are shared, and only up to what it still has.
    Ledger ledger;
    AddItems(ledger, Linen, 3);
    Check(SharedCount(ledger, Linen, 8) == 3, "looted linen is shared");
    Check(SharedCount(ledger, Linen, 2) == 2, "never more than the bot still has");
    Check(SharedCount(ledger, Sword, 1) == 0, "what it had before is not shared");

    // Items the player gave it count the same way.
    AddItems(ledger, Sword, 1);
    Check(Covers(ledger, {{Sword, 1}, {Linen, 3}}, 0, 0), "a trade of shared items is covered");
    Check(!Covers(ledger, {{Linen, 4}}, 0, 0), "one more than shared is not covered");
    Check(!Covers(ledger, {{Linen, 1}, {7073, 1}}, 0, 0), "a trade with one unshared item is not covered");

    // Gold: what it gained while grouped, at most what it still has; never its whole purse.
    AddGold(ledger, 500);
    Check(GoldAllowance(ledger, 10000) == 500, "gold gained is the allowance");
    Check(GoldAllowance(ledger, 200) == 200, "allowance is capped by the bot's money");
    Check(Covers(ledger, {}, 500, 10000), "the gained gold is covered");
    Check(!Covers(ledger, {}, 501, 10000), "a copper more than gained is not covered");
    Check(!Covers(ledger, {}, 300, 200), "more than the bot has is not covered");

    // After the trade, what was taken is no longer shared.
    Take(ledger, {{Linen, 2}, {Sword, 1}}, 300);
    Check(SharedCount(ledger, Linen, 10) == 1, "taken linen leaves the rest shared");
    Check(SharedCount(ledger, Sword, 1) == 0, "the taken sword is no longer shared");
    Check(ledger.gold == 200, "taken gold comes off the allowance");
    Take(ledger, {}, 1000);
    Check(ledger.gold == 0, "the allowance never goes below nothing");

    // A shared item the bot sold is trimmed, so finding another later on its own is not shared.
    Trim(ledger, Linen, 0);
    Check(SharedCount(ledger, Linen, 5) == 0, "sold shared items are forgotten");
    Check(ledger.Empty(), "nothing left shared");

    // Nothing shared and no gold: an empty trade is still covered (it gives nothing away).
    Check(Covers(Ledger{}, {}, 0, 100), "an empty offer is covered");

    std::cout << checks << " checks, " << failures << " failed\n";
    return failures ? 1 : 0;
}
