/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: a duel challenge still requires native human acceptance.
#ifndef PLAYERBOTS_DIALOGUE_DUEL_H
#define PLAYERBOTS_DIALOGUE_DUEL_H
#include "PlayerbotDialogueTypes.h"
class Player;
class PlayerbotAI;
namespace PlayerbotDialogue
{
struct DuelExecution
{
    Request request;
    uint64_t startedMs = 0;
    uint64_t arbiter = 0;
};
bool CanOfferDuel(Player* bot, Player* human, uint64_t nowMs);
Outcome StartDuel(Player* bot, PlayerbotAI* ai, DuelExecution& execution, uint64_t nowMs);
Outcome PollDuel(Player* bot, DuelExecution& execution, uint64_t nowMs);
void CancelDuel(Player* bot, DuelExecution const& execution);
}  // namespace PlayerbotDialogue
#endif
