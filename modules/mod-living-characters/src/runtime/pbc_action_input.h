// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Living Azeroth: grounded action options, built once for an admitted human
// contribution.
#ifndef PBC_ACTION_INPUT_H
#define PBC_ACTION_INPUT_H

#include <map>
#include <set>

#include "PlayerbotDialogueServices.h"
#include "PlayerbotDialogueSocial.h"
#include "pbc_actions.h"
#include "pbc_action_followup.h"
#include "pbc_game.h"
#include "pbc_model.h"

namespace PBC
{
struct ActionFrame
{
    std::vector<ActionRequest> actors;
    std::map<std::string, PlayerbotDialogue::Entity> targets;
    std::map<std::string, std::vector<PlayerbotDialogue::Entity>> assistTargets;
    std::map<std::string, std::vector<PlayerbotDialogue::Entity>> protectTargets;
    std::map<std::string, uint32_t> durations;
    std::map<uint64_t, std::vector<PlayerbotDialogue::InventoryItem>> inventory;
    std::map<uint64_t, std::vector<PlayerbotDialogue::InventoryItem>> equipped;
    std::map<uint64_t, std::vector<PlayerbotDialogue::InventoryItem>> savedEquipment;
    std::map<std::string, uint32_t> equipmentSlots;
    std::map<std::string, uint32_t> quantities;
    std::map<uint64_t, std::map<std::string, uint32_t>> abilities;
    std::map<uint64_t, std::vector<PlayerbotDialogue::ServiceOffer>> services;
    std::map<std::string, std::pair<uint64_t, bool>> moneyLimits;
    std::map<uint64_t, std::vector<PlayerbotDialogue::QuestOffer>> quests;
    std::map<uint64_t, std::map<uint32_t, std::string>> roles;
    std::map<uint64_t, std::map<uint32_t, std::string>> specs;
    std::map<uint64_t, ActionRequest> supplyOffers;
    struct CombatReference
    {
        std::string recipient;
        std::string intent;
        std::string target;
    };
    std::optional<CombatReference> combatReference;
    std::string factsJson = "{}";
    std::vector<SelectionState::Question> questions;
};

std::map<std::string, uint32_t> ActionDurations(std::string const& contribution);
ActionFrame CaptureActionFrame(GameAudience const& audience, std::string const& contribution, std::string const& id,
                               uint64_t sequence, uint64_t nowMs);
void BindCombatReference(ActionFrame& frame, RecentCombatOrder const& recent, std::string const& contribution);
std::vector<std::vector<ActionRequest>> ResolveActionDecisions(ActionFrame const& frame, ModelReply const& reply,
                                                               std::string& clarification,
                                                               ActionFeedback* feedback = nullptr);
}  // namespace PBC

#endif
