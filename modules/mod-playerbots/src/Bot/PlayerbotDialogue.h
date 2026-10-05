/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: immutable requests cross to the bot's owning map thread.
#ifndef PLAYERBOTS_DIALOGUE_H
#define PLAYERBOTS_DIALOGUE_H

#include <optional>
#include <vector>

#include "PlayerbotDialogueTypes.h"

class Player;
class Unit;

class PlayerbotDialogueBridge
{
public:
    static void Configure(bool enabled);
    static bool Enabled();
    static bool HasPendingWork(uint64_t bot);
    // True while the bot factory is creating this character's history (not gameplay).
    static bool IsSettingUp(Player* player);
    static bool AllowsInitiative(Player* bot, Player* observer);
    static bool AllowsInvitation(Player* bot, Player* observer);
    static bool AllowsInitiativeApproach(Player* bot);
    static std::optional<PlayerbotDialogue::Request> PendingOffer(uint64_t bot, uint64_t human);
    static std::vector<PlayerbotDialogue::InventoryItem> SavedEquipment(uint64_t bot);
    static PlayerbotDialogue::Status PreparationStatus(uint64_t bot, std::string const& group,
                                                       uint64_t authorityVersion);
    static void CompletePreparedPull(PlayerbotDialogue::Request const& request);
    static void Suspend();  // Cancel pending work and restore temporary orders on owning map ticks.
    static void SetTime(uint64_t nowMs);
    static PlayerbotDialogue::Entity Snapshot(Unit* unit);
    static bool Matches(Unit* unit, PlayerbotDialogue::Entity const& entity);
    static void InvalidateLife(Unit* unit);
    static void RegisterScripts();
    static uint64_t Revision(uint64_t bot);
    static bool Enqueue(PlayerbotDialogue::Request const& request);
    static void Cancel(uint64_t bot, std::string const& requestId);
    static void Interrupt(uint64_t bot);  // Explicit command, ownership change or logout.
    static void ExplicitCommand(uint64_t bot, std::string const& command);
    static void Release(Player* bot);  // Before native logout/save; caller owns this Player.
    static void FilterPersistence(Player* bot, std::vector<std::string>& values, std::vector<std::string>& combat,
                                  std::vector<std::string>& nonCombat);
    static void ExecuteSocial(PlayerbotDialogue::Request const& request);  // World thread only.
    static void Update(Player* bot);                                       // Called on its map thread before native AI.
    static std::vector<PlayerbotDialogue::Outcome> Drain();
};

#endif
