// SPDX-License-Identifier: GPL-2.0-or-later
#include "pbc_listener.h"

#include <utility>

namespace PBC
{
namespace
{
constexpr std::array<std::pair<char const*, ListenChannel>, 4> Channels{{
    {"party", ListenChannel::Party},
    {"nearby", ListenChannel::Nearby},
    {"guild", ListenChannel::Guild},
    {"general", ListenChannel::General},
}};
constexpr std::array<char const*, 3> HearingNames{"chatty", "spoken", "silent"};
constexpr std::array<char const*, 4> RemarkNames{"often", "sometimes", "rarely", "never"};
constexpr std::array<char const*, 3> BanterNames{"off", "sometimes", "often"};

// A whole number that is not negative, whether the JSON holds it as signed or unsigned.
std::optional<uint64_t> Count(pbc_json const& value)
{
    if (value.is_number_unsigned())
        return value.get<uint64_t>();
    if (value.is_number_integer() && value.get<int64_t>() >= 0)
        return static_cast<uint64_t>(value.get<int64_t>());
    return std::nullopt;
}

template <typename Enum, std::size_t N>
std::optional<Enum> Parse(pbc_json const& value, std::array<char const*, N> const& names)
{
    if (!value.is_string())
        return std::nullopt;
    for (std::size_t i = 0; i < N; ++i)
        if (value.get<std::string>() == names[i])
            return static_cast<Enum>(i);
    return std::nullopt;
}
}

std::optional<ListenChannel> ChannelOfAudience(std::string const& label)
{
    if (label == "party" || label == "raid")
        return ListenChannel::Party;
    if (label == "say" || label == "yell" || label == "emote")
        return ListenChannel::Nearby;
    if (label == "guild")
        return ListenChannel::Guild;
    if (label == "general")
        return ListenChannel::General;
    return std::nullopt;
}

pbc_json ListenerJson(ListenerSettings const& settings)
{
    pbc_json json;
    for (auto const& [name, channel] : Channels)
        json[name] = HearingNames[static_cast<std::size_t>(settings.On(channel))];
    json["remarks"] = RemarkNames[static_cast<std::size_t>(settings.remarks)];
    json["banter"] = BanterNames[static_cast<std::size_t>(settings.banter)];
    json["reading"] = settings.readingWordsPerMinute;
    json["turns"] = settings.maxTurns;
    return json;
}

bool ApplyListenerJson(ListenerSettings& settings, pbc_json const& changes, std::string& error)
{
    if (!changes.is_object())
    {
        error = "settings must be an object";
        return false;
    }
    ListenerSettings next = settings;
    for (auto const& [key, value] : changes.items())
    {
        bool known = false;
        for (auto const& [name, channel] : Channels)
            if (key == name)
            {
                auto hearing = Parse<Hearing>(value, HearingNames);
                if (!hearing)
                {
                    error = key + " must be chatty, spoken or silent";
                    return false;
                }
                next.hearing[static_cast<std::size_t>(channel)] = *hearing;
                known = true;
            }
        if (known)
            continue;
        if (key == "remarks")
        {
            auto remarks = Parse<Remarks>(value, RemarkNames);
            if (!remarks)
            {
                error = "remarks must be often, sometimes, rarely or never";
                return false;
            }
            next.remarks = *remarks;
        }
        else if (key == "banter")
        {
            auto banter = Parse<Banter>(value, BanterNames);
            if (!banter)
            {
                error = "banter must be off, sometimes or often";
                return false;
            }
            next.banter = *banter;
        }
        else if (key == "reading")
        {
            auto words = Count(value);
            if (!words || (*words != 0 && (*words < 60 || *words > 400)))
            {
                error = "reading must be 0 (the realm's) or 60-400 words a minute";
                return false;
            }
            next.readingWordsPerMinute = static_cast<uint16_t>(*words);
        }
        else if (key == "turns")
        {
            auto turns = Count(value);
            if (!turns || *turns > 6)
            {
                error = "turns must be 0 (the realm's) or 1-6";
                return false;
            }
            next.maxTurns = static_cast<uint8_t>(*turns);
        }
        else
        {
            error = "unknown setting " + key;
            return false;
        }
    }
    settings = next;
    return true;
}
}
