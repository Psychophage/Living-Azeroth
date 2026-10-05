/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: exact inventory operations through normal game handlers.
#ifndef PLAYERBOTS_DIALOGUE_INVENTORY_H
#define PLAYERBOTS_DIALOGUE_INVENTORY_H

#include "PlayerbotDialogueTypes.h"

class Player;
class PlayerbotAI;
class WorldPacket;

namespace PlayerbotDialogue
{
struct InventoryExecution
{
    Request request;
    uint64_t startedMs = 0;
};

std::vector<InventoryItem> InventorySnapshot(PlayerbotAI* ai);
std::vector<InventoryItem> SupplyOptions(PlayerbotAI* ai);
Outcome StartInventory(Player* bot, PlayerbotAI* ai, InventoryExecution& execution, uint64_t nowMs);
Outcome PollInventory(Player* bot, InventoryExecution const& execution, uint64_t nowMs);
void CancelInventory(Player* bot, Request const& request);
void ObserveInventoryPacket(Player* receiver, WorldPacket const* packet);
bool AllowManagedTrade(Player* bot);  // Also preserves the native trading policy in TradeStatusAction.
}  // namespace PlayerbotDialogue
#endif
