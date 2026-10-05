/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: native quest and group operations, with normal consent.
#ifndef PLAYERBOTS_DIALOGUE_SOCIAL_H
#define PLAYERBOTS_DIALOGUE_SOCIAL_H

#include "PlayerbotDialogueTypes.h"
class Player;
class Unit;

namespace PlayerbotDialogue
{
struct QuestOffer
{
    Kind kind;
    uint32_t id;
    Entity provider;
    std::string name;
};
std::vector<QuestOffer> QuestOffers(Player* bot, Unit* provider);
Outcome RunSocial(Player* bot, Request const& request, uint64_t nowMs);
}  // namespace PlayerbotDialogue
#endif
