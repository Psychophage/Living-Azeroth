/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_CHANGESTRATEGYACTION_H
#define PLAYERBOTS_CHANGESTRATEGYACTION_H

#include "Action.h"
#include "PlayerbotAI.h"

// Living Azeroth: a strategy change exactly as the "co" and "nc" chat commands make it (their rule for random
// bots' looting, and saving the result), for callers that must not go through the chat command queue, where a
// second change waiting behind the first would replace it. False with a refusal when it is not allowed.
bool ApplyStrategyChange(PlayerbotAI* botAI, std::string const& change, BotState state, std::string& refusal,
                         bool save = true);

class ChangeCombatStrategyAction : public Action
{
public:
    ChangeCombatStrategyAction(PlayerbotAI* botAI, std::string const name = "co") : Action(botAI, name) {}

    bool Execute(Event event) override;
};

class ChangeNonCombatStrategyAction : public Action
{
public:
    ChangeNonCombatStrategyAction(PlayerbotAI* botAI) : Action(botAI, "nc") {}

    bool Execute(Event event) override;
};

class ChangeDeadStrategyAction : public Action
{
public:
    ChangeDeadStrategyAction(PlayerbotAI* botAI) : Action(botAI, "de") {}

    bool Execute(Event event) override;
};

#endif
