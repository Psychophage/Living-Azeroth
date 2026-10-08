// SPDX-License-Identifier: GPL-2.0-or-later
// Living Azeroth: how much each player hears from characters, chosen per player.
#ifndef PBC_LISTENER_H
#define PBC_LISTENER_H

#include "pbc_json.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace PBC
{
// What reaches a player on one channel: everything, only replies to what they said
// themselves, or nothing. Orders still work in every case; this is speech only.
enum class Hearing : uint8_t
{
    Chatty,
    SpokenTo,
    Silent,
};

enum class ListenChannel : uint8_t
{
    Party,
    Nearby,
    Guild,
    General,
};

// How often characters make remarks of their own to this player, against the realm's
// AmbientMinMinutes/AmbientMaxMinutes ("sometimes").
enum class Remarks : uint8_t
{
    Often,
    Sometimes,
    Rarely,
    Never,
};

// Combat banter against the realm's CombatBanterChance ("sometimes").
enum class Banter : uint8_t
{
    Off,
    Sometimes,
    Often,
};

struct ListenerSettings
{
    std::array<Hearing, 4> hearing{Hearing::Chatty, Hearing::Chatty, Hearing::Chatty, Hearing::Chatty};
    Remarks remarks = Remarks::Sometimes;
    Banter banter = Banter::Sometimes;
    uint16_t readingWordsPerMinute = 0;  // 0: the realm's ReadingWordsPerMinute
    uint8_t maxTurns = 0;                // 0: the realm's MaxTurns

    Hearing On(ListenChannel channel) const { return hearing[static_cast<std::size_t>(channel)]; }
};

// The channel an audience label belongs to; whispers have none and are always heard.
std::optional<ListenChannel> ChannelOfAudience(std::string const& label);

pbc_json ListenerJson(ListenerSettings const& settings);
// Applies the fields present in `changes`; on a bad field, changes nothing.
bool ApplyListenerJson(ListenerSettings& settings, pbc_json const& changes, std::string& error);
}

#endif
