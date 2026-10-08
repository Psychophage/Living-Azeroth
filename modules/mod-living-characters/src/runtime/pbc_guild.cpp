// SPDX-License-Identifier: GPL-2.0-or-later
#include "pbc_guild.h"

#include <array>
#include <utility>

namespace PBC
{
namespace
{
constexpr std::size_t FieldLimit = 400;

std::array<std::pair<char const*, std::string GuildIdentity::*>, 5> const Fields{{
    {"purpose", &GuildIdentity::purpose},
    {"values", &GuildIdentity::values},
    {"traditions", &GuildIdentity::traditions},
    {"ambitions", &GuildIdentity::ambitions},
    {"voice", &GuildIdentity::voice},
}};

std::string Trimmed(std::string text)
{
    auto start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return {};
    auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}
}

bool GuildIdentity::Empty() const
{
    for (auto const& [name, field] : Fields)
        if (!(this->*field).empty())
            return false;
    return true;
}

std::string GuildIdentity::Describe() const
{
    std::string text;
    auto add = [&](char const* label, std::string const& value)
    {
        if (value.empty())
            return;
        if (!text.empty())
            text += ' ';
        text += label + value;
        if (text.back() != '.' && text.back() != '!' && text.back() != '?')
            text += '.';
    };
    add("What the guild is for: ", purpose);
    add("Its members value: ", values);
    add("Traditions: ", traditions);
    add("Current ambitions: ", ambitions);
    add("How its members speak: ", voice);
    if (!text.empty())
        text += " This is the guild's own account of itself, written by its officers; shared guild "
                "notices are reported information, not every member's personal experience.";
    return text;
}

pbc_json GuildIdentityJson(GuildIdentity const& identity)
{
    pbc_json json;
    for (auto const& [name, field] : Fields)
        json[name] = identity.*field;
    json["report_hours"] = identity.reportHours;
    return json;
}

bool ApplyGuildIdentityJson(GuildIdentity& identity, pbc_json const& changes, std::string& error)
{
    if (!changes.is_object())
    {
        error = "the identity must be an object";
        return false;
    }
    GuildIdentity next = identity;
    for (auto const& [key, value] : changes.items())
    {
        bool known = false;
        for (auto const& [name, field] : Fields)
            if (key == name)
            {
                if (!value.is_string() || value.get<std::string>().size() > FieldLimit)
                {
                    error = key + " must be text of at most 400 characters";
                    return false;
                }
                next.*field = Trimmed(value.get<std::string>());
                known = true;
            }
        if (known)
            continue;
        if (key == "report_hours")
        {
            bool whole = value.is_number_unsigned() || (value.is_number_integer() && value.get<int64_t>() >= 0);
            if (!whole || value.get<uint64_t>() < 1 || value.get<uint64_t>() > 720)
            {
                error = "report_hours must be 1-720";
                return false;
            }
            next.reportHours = static_cast<uint32_t>(value.get<uint64_t>());
        }
        else
        {
            error = "unknown field " + key;
            return false;
        }
    }
    identity = next;
    return true;
}

std::map<uint32_t, GuildIdentity>& GuildIdentities()
{
    static std::map<uint32_t, GuildIdentity> identities;
    return identities;
}
}
