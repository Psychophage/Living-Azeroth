/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: correlate support actions with native effects, not cast admission.
#ifndef PLAYERBOTS_DIALOGUE_SUPPORT_H
#define PLAYERBOTS_DIALOGUE_SUPPORT_H

#include "PlayerbotDialogueCombat.h"

class WorldPacket;
class Unit;
namespace PlayerbotDialogue
{
bool HasUsefulSupport(Player* bot, PlayerbotAI* ai, Unit* target, Kind kind);
Outcome StartSupport(Player* bot, PlayerbotAI* ai, CombatExecution& execution, uint64_t nowMs);
Outcome PollSupport(Player* bot, PlayerbotAI* ai, CombatExecution& execution, uint64_t nowMs);
void CancelSupport(PlayerbotAI* ai, Request const& request);
void ObserveSupportPacket(Player* receiver, WorldPacket const* packet);
void ObserveResurrection(Player* player, float restoredFraction);
}  // namespace PlayerbotDialogue
#endif
