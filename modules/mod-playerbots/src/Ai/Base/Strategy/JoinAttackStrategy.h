/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_JOINATTACKSTRATEGY_H
#define PLAYERBOTS_JOINATTACKSTRATEGY_H

#include "Strategy.h"

class PlayerbotAI;

// Living Azeroth: read by the "attackers" value. With it, the target of a human master's
// auto-attack counts as an enemy as soon as the attack starts, before the first hit.
class JoinAttackStrategy : public Strategy
{
public:
    JoinAttackStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    std::string const getName() override { return "join attack"; }
};

#endif
