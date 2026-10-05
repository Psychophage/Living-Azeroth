/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: native prices and progression, without free-gold bot shortcuts.
#ifndef PLAYERBOTS_DIALOGUE_SERVICES_H
#define PLAYERBOTS_DIALOGUE_SERVICES_H

#include <optional>

#include "PlayerbotDialogueTypes.h"

class Creature;
class Player;
class PlayerbotAI;

namespace PlayerbotDialogue
{
struct ServiceOffer
{
    Kind kind;
    Entity provider;
    uint32_t objectId = 0;
    uint32_t slot = 0;
    uint32_t bundleSize = 1;
    uint64_t copper = 0;
    uint32_t baseCopper = 0;
    float discount = 1.0f;
    std::string name;
    uint32_t talentSpec = 0;
    std::vector<TalentRank> talentPlan = {};
};

std::vector<ServiceOffer> ServiceOffers(Player* bot, Creature* provider, bool train, bool repair, bool buy,
                                        bool talents);
std::optional<uint64_t> QuoteService(Player* bot, Creature* provider, Request const& request);
Outcome RunService(Player* bot, PlayerbotAI* ai, Request const& request, uint64_t nowMs);
}  // namespace PlayerbotDialogue
#endif
