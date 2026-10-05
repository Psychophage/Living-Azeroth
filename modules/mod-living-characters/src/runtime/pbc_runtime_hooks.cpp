// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
#include "PopulationMgr.h"
#include "pbc_runtime.h"
#include "PlayerScript.h"
#include "Player.h"
#include "Creature.h"
#include "QuestDef.h"
#include <mutex>
#include <set>

namespace
{
class CharacterHooks final : public PlayerScript
{
public:
    CharacterHooks() : PlayerScript("LivingCharactersHooks",
        {PLAYERHOOK_ON_AFTER_SEND_CHAT_MESSAGE,
         PLAYERHOOK_ON_TEXT_EMOTE, PLAYERHOOK_ON_PLAYER_ENTER_COMBAT, PLAYERHOOK_ON_PLAYER_LEAVE_COMBAT,
         PLAYERHOOK_ON_LOGOUT,
         PLAYERHOOK_ON_CREATURE_KILL, PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST, PLAYERHOOK_ON_PLAYER_QUEST_ACCEPT}) { }

    void OnPlayerAfterSendChatMessage(Player* player, uint32 type, uint32 language,
        std::string const& message, Player* receiver, Channel* channel) override
    {
        if (receiver && player && language != LANG_ADDON)
        {
            Player* human = player->GetSession()->IsBot() ? receiver : player;
            Player* bot = player->GetSession()->IsBot() ? player : receiver;
            if (!human->GetSession()->IsBot() && bot->GetSession()->IsBot())
                PlayerbotPopulationMgr::Instance().Interaction(human->GetGUID().GetCounter(), bot->GetGUID().GetCounter());
        }
        PBC::ObserveChat(player, type, language, message, receiver, channel);
    }

    void OnPlayerTextEmote(Player* player, uint32 emote, uint32, ObjectGuid target) override
    {
        PBC::ObserveEmote(player, emote, target);
    }

    void OnPlayerEnterCombat(Player* player, Unit* enemy) override
    {
        {
            std::lock_guard lock(_combatMutex);
            if (!_combatants.insert(player->GetGUID()).second)
                return;
        }
        PBC::ObserveWorldEvent(player, player->GetName() + " entered combat" +
            (enemy ? " with " + enemy->GetName() : std::string()) + ".", false, true, enemy);
    }

    void OnPlayerLeaveCombat(Player* player) override
    {
        {
            std::lock_guard lock(_combatMutex);
            // Unit::CombatStop also invokes this hook when already out of combat.
            // Do not invent repeated departures or cancel scenes for those calls.
            if (!_combatants.erase(player->GetGUID()))
                return;
        }
        PBC::ObserveWorldEvent(player, player->GetName() + " is no longer in combat.");
    }

    void OnPlayerLogout(Player* player) override
    {
        std::lock_guard lock(_combatMutex);
        _combatants.erase(player->GetGUID());
    }

    void OnPlayerCreatureKill(Player* player, Creature* killed) override
    {
        PBC::ObserveWorldEvent(player, player->GetName() + " defeated " + killed->GetName() + ".");
    }

    void OnPlayerQuestAccept(Player* player, Quest const* quest) override
    {
        PBC::ObserveWorldEvent(player, player->GetName() + " accepted the quest: " + quest->GetTitle(), true);
    }

    void OnPlayerCompleteQuest(Player* player, Quest const* quest) override
    {
        PBC::ObserveWorldEvent(player, player->GetName() + " completed the quest: " + quest->GetTitle(), true);
    }

private:
    std::mutex _combatMutex;
    std::set<ObjectGuid> _combatants;
};
}

void AddPBCCharacterHooks()
{
    new CharacterHooks();
}
