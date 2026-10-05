// SPDX-License-Identifier: GPL-2.0-or-later
// Living Azeroth changes; upstream authorship retained in AUTHORS.
#ifndef PLAYERBOTS_DIALOGUE_PERFORMANCE_H
#define PLAYERBOTS_DIALOGUE_PERFORMANCE_H

#include "PlayerbotDialogueTypes.h"

class Player;

namespace PlayerbotDialogue
{
struct PerformanceState
{
    std::vector<InventoryItem> equipment;
    uint64_t danceEndsMs = 0;
    bool ownsMovement = false;
};

std::vector<InventoryItem> SavedPerformanceEquipment(Player* bot);
bool IsPerformanceEquipment(Player* bot, uint64_t itemGuid);
Outcome Perform(Player* bot, Request const& request, PerformanceState& state, uint64_t nowMs);
bool UpdatePerformance(Player* bot, PerformanceState& state, uint64_t nowMs);
void StopPerformance(Player* bot, PerformanceState& state);
}  // namespace PlayerbotDialogue
#endif
