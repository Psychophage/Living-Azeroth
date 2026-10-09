// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef BC_BRIDGE_H
#define BC_BRIDGE_H

#include "Define.h"
#include "ObjectGuid.h"
#include "bc_frames.h"
#include "pbc_json.h"
#include <string>
#include <unordered_map>
#include <vector>

class Player;

namespace BotControl
{
// The addon channel and its permission checks. Orders go to Playerbots as the same
// commands its chat uses, and are allowed exactly when Playerbots would allow them
// by whisper; the bridge keeps no bot or dialogue policy of its own.
//
// Requests arrive while the world processes chat and state is read in the world
// update, both outside the map updates, so bot state is never read mid-update.
class Bridge
{
public:
    static Bridge& Instance();

    void LoadConfig();
    // A whisper the player sent to themselves in the addon language; true when it was ours.
    bool Receive(Player* player, std::string const& text);
    void Update(uint32 diff);
    void Forget(Player* player);

private:
    struct Pending
    {
        ObjectGuid player;
        ObjectGuid bot;
        pbc_json id;
        std::string order;
        bool on = false;
        uint32 waited = 0;
    };

    void Handle(Player* player, pbc_json const& request);
    void Hello(Player* player, pbc_json const& id);
    void Bots(Player* player, pbc_json const& id);
    void Who(Player* player, pbc_json const& request);
    void Order(Player* player, pbc_json const& request);
    void Hearing(Player* player, pbc_json const& request);
    void Guild(Player* player, pbc_json const& request);
    void Rumours(Player* player, pbc_json const& request);
    void Why(Player* player, pbc_json const& request);
    void Budget(Player* player, pbc_json const& request);
    void Roster(Player* player, pbc_json const& request);
    void Inspect(Player* player, pbc_json const& request);
    void Bring(Player* player, pbc_json const& request, bool bring);
    void Watch();

    void Send(Player* player, pbc_json const& message);
    void Reply(Player* player, pbc_json const& id, pbc_json body);
    void Fail(Player* player, pbc_json const& id, std::string const& error);

    bool _enabled = false;
    uint32 _timeoutMs = 10000;
    uint32 _sinceWatch = 0;
    uint32 _messages = 0;
    std::unordered_map<ObjectGuid, Assembler> _assemblers;
    // Players whose addon said hello, and the last state each was sent per bot.
    std::unordered_map<ObjectGuid, std::unordered_map<ObjectGuid, std::string>> _watching;
    std::vector<Pending> _pending;
};
}

#endif
