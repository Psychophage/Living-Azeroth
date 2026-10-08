// SPDX-License-Identifier: GPL-2.0-or-later
// Living Azeroth: a player guild's own identity, written by its officers, which its
// members carry into conversation the way a town watch carries its description.
#ifndef PBC_GUILD_H
#define PBC_GUILD_H

#include "pbc_json.h"
#include <cstdint>
#include <map>
#include <string>

namespace PBC
{
struct GuildIdentity
{
    std::string purpose;     // what the guild is for
    std::string values;      // what its members value
    std::string traditions;  // traditions and habits
    std::string ambitions;   // what it is working towards now
    std::string voice;       // how its members speak
    uint32_t reportHours = 72;  // how long its rumours last (1-720)

    bool Empty() const;
    // The description members receive; empty when nothing has been written.
    std::string Describe() const;
};

pbc_json GuildIdentityJson(GuildIdentity const& identity);
// Applies the fields present in `changes`; on a bad field, changes nothing.
bool ApplyGuildIdentityJson(GuildIdentity& identity, pbc_json const& changes, std::string& error);

// Written guild identities by native guild ID. Read and changed on the world thread only.
std::map<uint32_t, GuildIdentity>& GuildIdentities();
}

#endif
