/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: tactical roles and existing specializations are separate from talent rebuilding.
#ifndef PLAYERBOTS_DIALOGUE_PROGRESSION_H
#define PLAYERBOTS_DIALOGUE_PROGRESSION_H

#include <map>

#include "PlayerbotDialogueTypes.h"

class Player;
class PlayerbotAI;

namespace PlayerbotDialogue
{
std::map<uint32_t, std::string> AvailableRoles(Player* bot, PlayerbotAI* ai);
Outcome ChangeRole(Player* bot, PlayerbotAI* ai, Request const& request, uint64_t nowMs);
std::map<uint32_t, std::string> AvailableSpecs(Player* bot);
struct SpecExecution
{
    Request request;
    uint32_t spell = 0;
    uint64_t castAtMs = 0;
};
Outcome StartSpec(Player* bot, SpecExecution& execution, uint64_t nowMs);
Outcome PollSpec(Player* bot, SpecExecution const& execution, uint64_t nowMs);
void CancelSpec(Player* bot, SpecExecution const& execution);
}  // namespace PlayerbotDialogue
#endif
