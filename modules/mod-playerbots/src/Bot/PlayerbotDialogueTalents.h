/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: validated native talent presets with normal trainer payment.
#ifndef PLAYERBOTS_DIALOGUE_TALENTS_H
#define PLAYERBOTS_DIALOGUE_TALENTS_H
#include <map>

#include "PlayerbotDialogueTypes.h"
class Player;
namespace PlayerbotDialogue
{
std::vector<TalentRank> TalentPlan(Player* bot, uint32_t preset);
std::map<uint32_t, std::string> AvailableTalentPresets(Player* bot);
bool HasAllocatedTalents(Player* bot);
bool ApplyTalentPlan(Player* bot, Request const& request);
}  // namespace PlayerbotDialogue
#endif
