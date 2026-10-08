// SPDX-License-Identifier: GPL-2.0-or-later
#include "bc_bridge.h"

#include "Player.h"
#include "ScriptMgr.h"

namespace
{
class BotControlPlayerScript : public PlayerScript
{
public:
    BotControlPlayerScript()
        : PlayerScript("BotControlPlayerScript", {PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT, PLAYERHOOK_ON_LOGOUT})
    {
    }

    // The addon talks to the server by whispering its own player; ours never reaches the client.
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& msg, Player* receiver) override
    {
        if (type != CHAT_MSG_WHISPER || language != LANG_ADDON || receiver != player)
            return true;
        return !BotControl::Bridge::Instance().Receive(player, msg);
    }

    void OnPlayerLogout(Player* player) override
    {
        BotControl::Bridge::Instance().Forget(player);
    }
};

class BotControlWorldScript : public WorldScript
{
public:
    BotControlWorldScript() : WorldScript("BotControlWorldScript", {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_UPDATE})
    {
    }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        BotControl::Bridge::Instance().LoadConfig();
    }

    void OnUpdate(uint32 diff) override
    {
        BotControl::Bridge::Instance().Update(diff);
    }
};
}

// The module loader calls Add<directory name>Scripts().
void Addmod_bot_controlScripts()
{
    new BotControlPlayerScript();
    new BotControlWorldScript();
}
