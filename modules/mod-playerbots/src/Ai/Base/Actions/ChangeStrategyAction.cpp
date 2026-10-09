/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ChangeStrategyAction.h"
#include "Event.h"
#include "PlayerbotRepository.h"
#include "Playerbots.h"

// Helper function for prefixes used by combat and non-combat strategy commands.
static void HandleStrategyCommon(PlayerbotAI* botAI, std::string const& text, BotState state)
{
    std::vector<std::string> splitted = split(text, ',');
    for (std::vector<std::string>::iterator i = splitted.begin(); i != splitted.end(); i++)
    {
        char const* name = i->c_str();
        switch (name[0])
        {
            case '+':
            case '-':
            case '~':
                PlayerbotRepository::instance().Save(botAI);
                break;
            case '!':
                botAI->SelectiveResetStrategies(state);
                PlayerbotRepository::instance().Save(botAI);
                break;
            case '?':
                break;
        }
    }
}

bool ApplyStrategyChange(PlayerbotAI* botAI, std::string const& change, BotState state, std::string& refusal,
                         bool save)
{
    uint32 account = botAI->GetBot()->GetSession()->GetAccountId();
    if (state == BOT_STATE_NON_COMBAT && sPlayerbotAIConfig.IsInRandomAccountList(account) && botAI->GetMaster() &&
        !botAI->GetMaster()->CanBeGameMaster() && change.find("loot") != std::string::npos)
    {
        refusal = "You can change any strategy except loot";
        return false;
    }
    botAI->ChangeStrategy(change, state);
    if (save)
        HandleStrategyCommon(botAI, change, state);
    return true;
}

bool ChangeCombatStrategyAction::Execute(Event event)
{
    std::string const text = event.getParam();
    botAI->ChangeStrategy(text.empty() ? getName() : text, BOT_STATE_COMBAT);
    if (event.GetSource() == "co")
        HandleStrategyCommon(botAI, text, BOT_STATE_COMBAT);

    return true;
}

bool ChangeNonCombatStrategyAction::Execute(Event event)
{
    std::string const text = event.getParam();
    std::string refusal;
    if (!ApplyStrategyChange(botAI, text, BOT_STATE_NON_COMBAT, refusal, event.GetSource() == "nc"))
    {
        botAI->TellError(refusal);
        return false;
    }
    return true;
}

bool ChangeDeadStrategyAction::Execute(Event event)
{
    std::string const text = event.getParam();
    botAI->ChangeStrategy(text, BOT_STATE_DEAD);
    return true;
}
