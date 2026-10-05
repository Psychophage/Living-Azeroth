/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: native combat execution on the owning map thread.
#ifndef PLAYERBOTS_DIALOGUE_COMBAT_H
#define PLAYERBOTS_DIALOGUE_COMBAT_H

#include "PlayerbotDialogueTypes.h"

class Player;
class PlayerbotAI;

namespace PlayerbotDialogue
{
struct CombatExecution
{
    Request request;
    uint64_t startedMs = 0;
    uint32_t spellId = 0;
    bool castStarted = false;
    uint64_t castAtMs = 0;
    bool priorPullBack = false;
    float returnX = 0, returnY = 0, returnZ = 0;
};

Outcome StartCombat(Player* bot, PlayerbotAI* ai, CombatExecution& execution, uint64_t nowMs);
Outcome PollCombat(Player* bot, PlayerbotAI* ai, CombatExecution& execution, uint64_t nowMs);
void CancelCombat(PlayerbotAI* ai, Request const& request);
void FinishPartyPull(PlayerbotAI* ai, CombatExecution const& execution);
}  // namespace PlayerbotDialogue
#endif
