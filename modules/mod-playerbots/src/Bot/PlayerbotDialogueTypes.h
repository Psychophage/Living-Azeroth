/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: a typed boundary for dialogue requests. No model-authored command strings.
#ifndef PLAYERBOTS_DIALOGUE_TYPES_H
#define PLAYERBOTS_DIALOGUE_TYPES_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace PlayerbotDialogue
{
enum class Kind
{
    Follow,
    Stay,
    Approach,
    Regroup,
    Retreat,
    Formation,
    Resume,
    Attack,
    Pull,
    StopAttack,
    Assist,
    Protect,
    Buff,
    Heal,
    Cleanse,
    Resurrect,
    PetAttack,
    PetFollow,
    PetStay,
    PetPassive,
    PetDefensive,
    PetAggressive,
    Inspect,
    Use,
    Equip,
    Trade,
    Train,
    Repair,
    Buy,
    Role,
    Spec,
    Talents,
    ShareQuest,
    AcceptQuest,
    QuestHelp,
    Invite,
    Join,
    Leave,
    Duel,
    OfferSupplies,
    DeclineOffer,
    Cancel,
    Unequip,
    RestoreEquipment,
    Dance,
    StopDance
};

enum class Status
{
    Pending,
    Started,
    Completed,
    Failed,
    Cancelled,
    Unresolved
};

struct Entity
{
    uint64_t guid = 0;
    uint32_t map = 0;
    uint32_t instance = 0;
    uint32_t entry = 0;
    uint64_t incarnation = 0;
};

struct InventoryItem
{
    uint64_t guid = 0;
    uint32_t entry = 0;
    uint32_t count = 0;
    uint16_t position = 0;
    int32_t property = 0;
    std::string name;
    bool equipped = false;
    bool tradable = false;
};

struct TalentRank
{
    uint32_t talent = 0;
    uint32_t rank = 0;  // Zero-based native rank.
    bool operator==(TalentRank const&) const = default;
};

struct Request
{
    std::string id;
    std::string groupId;
    uint64_t sequence = 0;
    Entity actor;
    Entity requester;
    Entity subject;  // Friendly party member whose current fight is being assisted/protected.
    Entity target;
    uint64_t authorityVersion = 0;
    Kind kind = Kind::Stay;
    uint32_t objectId = 0;  // A grounded item, spell, quest or supported preset ID.
    uint32_t quantity = 0;
    // Explicit entry quantities for one consent-bound mixed-item trade. Empty
    // retains the existing single-entry contract for older/native callers.
    std::map<uint32_t, uint32_t> tradeQuantities;
    uint32_t durationMs = 0;  // Zero means no timer was requested.
    uint64_t copperLimit = 0;
    uint64_t deadlineMs = 0;
    bool voluntary = false;
    std::string offerId;               // Exact still-pending supply offer accepted/declined by this human.
    std::vector<InventoryItem> items;  // Exact owned stacks observed when the human made the request.
    std::string moneyBudget;           // Shared across recipients/intents unless the human explicitly said 'each'.
    uint64_t reservedCopper = 0;
    std::vector<uint32_t> trainingSpells;  // Exact offered entries, not arbitrary learned spell IDs.
    uint32_t vendorSlot = 0;
    uint64_t actorGroup = 0;
    uint64_t invitingLeader = 0;
    uint32_t talentSpec = 0;
    std::vector<TalentRank> talentPlan;  // Validated configured preset, pinned before inference.
    struct PreparedCompanion
    {
        Entity actor;
        uint64_t authorityVersion = 0;
    };
    std::vector<PreparedCompanion> preparedParty;
    bool returnToParty = false;
};

struct Outcome
{
    std::string id;
    Status status = Status::Pending;
    std::string detail;
    uint64_t occurredMs = 0;
    uint64_t spentCopper = 0;  // Observed native wallet decrease; unresolved outcomes retain their reservation.
};

inline bool Terminal(Status status)
{
    return status == Status::Completed || status == Status::Failed || status == Status::Cancelled ||
           status == Status::Unresolved;
}
}  // namespace PlayerbotDialogue

#endif
