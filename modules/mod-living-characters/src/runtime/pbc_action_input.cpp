// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
#include "pbc_action_input.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>
#include <limits>

#include "CellImpl.h"
#include "GridNotifiersImpl.h"
#include "ObjectAccessor.h"
#include "PlayerbotDialogue.h"
#include "PlayerbotDialogueInventory.h"
#include "PlayerbotDialogueProgression.h"
#include "Playerbots.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "pbc_json.h"

namespace PBC
{
namespace
{
struct VisibleActionTarget
{
    Player* observer;
    bool operator()(Unit* unit) const
    {
        return unit && unit->IsInWorld() && observer->IsInMap(unit) && observer->InSamePhase(unit) &&
               observer->IsWithinDistInMap(unit, 60.0f) && observer->CanSeeOrDetect(unit);
    }
};
}  // namespace

std::map<std::string, uint32_t> ActionDurations(std::string const& contribution)
{
    static std::regex const pattern(
        R"(\b([0-9]+|one|two|three|four|five|six|seven|eight|nine|ten|a|an)\s+(seconds?|minutes?|hours?)\b)",
        std::regex::icase);
    static std::map<std::string, uint32_t> const words = {{"one", 1},   {"a", 1},     {"an", 1},   {"two", 2},
                                                          {"three", 3}, {"four", 4},  {"five", 5}, {"six", 6},
                                                          {"seven", 7}, {"eight", 8}, {"nine", 9}, {"ten", 10}};
    auto text = contribution;
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    std::map<std::string, uint32_t> result;
    for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it)
    {
        if (it->position() && (text[it->position() - 1] == '.' || text[it->position() - 1] == '-'))
            continue;  // Never reinterpret the tail of a fractional/negative duration
                       // as an integer.
        auto quantity = (*it)[1].str();
        auto unit = (*it)[2].str();
        uint64_t number = 0;
        if (auto found = words.find(quantity); found != words.end())
            number = found->second;
        else
        {
            if (quantity.size() > 6)
                continue;
            number = std::stoul(quantity);
        }
        auto duration = number * (unit.starts_with("hour") ? 3600000 : unit.starts_with("minute") ? 60000 : 1000);
        if (duration && duration <= 3600000)
            result.emplace((*it).str(), static_cast<uint32_t>(duration));
    }
    return result;
}

ActionFrame CaptureActionFrame(GameAudience const& audience, std::string const& contribution, std::string const& id,
                               uint64_t sequence, uint64_t nowMs)
{
    ActionFrame frame;
    if (!PlayerbotDialogueBridge::Enabled() || audience.label == "general")
        return frame;
    auto requester = ObjectAccessor::FindPlayer(audience.anchor);
    if (!HasHumanConnection(requester))
        return frame;
    auto entity = PlayerbotDialogueBridge::Snapshot;
    pbc_json facts = {{"requester", "player:" + std::to_string(requester->GetGUID().GetCounter())},
                      {"requester_name", requester->GetName()},
                      {"controlled_companions", pbc_json::array()},
                      {"targets", pbc_json::array()},
                      {"carried_item_fields", {"id", "quantity"}},
                      {"worn_item_fields", {"id", "slot"}}};
    std::map<std::string, std::string> recipients = {
        {"NONE", "No requested operation or no addressed controlled companion."},
        {"ALL", "The request addresses the entire controlled companion group."},
        {"CLARIFY", "The intended companion is ambiguous or unauthorized."}};
    std::map<std::string, std::string> targets = {
        {"NONE",
         "No separate unit reference: movement relative to the human or implicit receipt of items by the requesting "
         "human."},
        {"CLARIFY", "A required unit reference is genuinely ambiguous or absent."}};
    std::map<std::string, std::string> items = {
        {"NONE", "No inventory item applies."},
        {"CLARIFY", "The item reference is ambiguous."},
        {"UNAVAILABLE", "A specific requested item is not among the supplied carried items."}};
    std::vector<std::string> slotNames = {"head", "neck",     "shoulders", "shirt",   "chest",   "waist",    "legs",
                                          "feet", "wrists",   "hands",     "finger1", "finger2", "trinket1", "trinket2",
                                          "back", "mainhand", "offhand",   "ranged",  "tabard"};
    frame.equipmentSlots.emplace("equipped:all", (1u << slotNames.size()) - 1);
    items.emplace("equipped:all", "The actor's currently equipped outfit as a set; remove to owned bags, not trade.");
    for (unsigned slot = 0; slot < slotNames.size(); ++slot)
    {
        auto key = "equipped:" + slotNames[slot];
        frame.equipmentSlots.emplace(key, 1u << slot);
        items.emplace(key, "The actor's currently equipped " + slotNames[slot] + " slot, for removal.");
    }
    std::map<std::string, std::string> abilities = {
        {"NONE", "No particular named spell was requested; native choice is allowed."},
        {"CLARIFY",
         "A specific spell/rank is requested but no listed ability "
         "matches; never substitute a different spell."}};
    std::string lower = contribution;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    // Admission does not depend on a vocabulary whitelist. Capture native
    // capabilities for controlled companions and nearby providers once per input;
    // interpretation decides which fields matter. Existing option/context ceilings
    // report overflow instead of silently discarding requested meanings.
    for (auto const& actor : audience.actors)
    {
        auto unit = ResolveActor(actor);
        if (!unit || !unit->IsInMap(requester) || !requester->InSamePhase(unit))
            continue;
        auto target = entity(unit);
        frame.targets.emplace(actor.identity.id, target);
        targets.emplace(actor.identity.id, "Visible " + actor.identity.name + " (" + actor.identity.kind + ")");
        facts["targets"].push_back({{"id", actor.identity.id},
                                    {"label", actor.identity.name},
                                    {"selected_at_input", requester->GetTarget() == unit->GetGUID()}});
        auto bot = unit->ToPlayer();
        auto ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
        if (!ai || ai->GetMaster() != requester || !bot->IsAlive() ||
            !ai->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_ALLOW_ALL, true, requester))
            continue;
        ActionRequest request;
        request.id = id + ":" + actor.identity.id;
        request.groupId = id;
        request.sequence = sequence;
        request.actor = target;
        request.requester = entity(requester);
        request.actorGroup = bot->GetGroup() ? bot->GetGroup()->GetGUID().GetRawValue() : 0;
        request.invitingLeader = bot->GetGroupInvite() ? bot->GetGroupInvite()->GetLeaderGUID().GetRawValue() : 0;
        request.authorityVersion = PlayerbotDialogueBridge::Revision(target.guid);
        request.deadlineMs = nowMs + 120000;
        frame.actors.push_back(request);
        auto owned = PlayerbotDialogue::InventorySnapshot(ai);
        for (auto const& item : owned)
            if (item.equipped)
                frame.equipped[target.guid].push_back(item);
        frame.savedEquipment[target.guid] = PlayerbotDialogueBridge::SavedEquipment(target.guid);
        if (auto offer = PlayerbotDialogueBridge::PendingOffer(target.guid, request.requester.guid))
            frame.supplyOffers.emplace(target.guid, *offer);
        frame.roles[target.guid] = PlayerbotDialogue::AvailableRoles(bot, ai);
        frame.specs[target.guid] = PlayerbotDialogue::AvailableSpecs(bot);
        recipients.emplace(actor.identity.id, "Only " + actor.identity.name + ", a controlled companion.");
        // The selector needs compact current facts for intent/role selection.
        // Shared world progression is supplied once; detailed biography, money
        // and achievements remain in the individual character context.
        auto nativeFacts = pbc_json::parse(actor.factsJson);
        if (nativeFacts.contains("realm_phase") && !facts.contains("realm_phase"))
            facts["realm_phase"] = nativeFacts["realm_phase"];
        for (auto key : {"realm_phase", "group_id", "subgroup", "money_copper", "achievements"})
            nativeFacts.erase(key);
        facts["controlled_companions"].push_back(
            {{"id", actor.identity.id},
             {"label", actor.identity.name},
             {"addressed", actor.addressed || audience.whisperTarget == actor.guid},
             {"holding_position", ai->HasStrategy("stay", BOT_STATE_NON_COMBAT)},
             {"dancing", bot->GetUInt32Value(UNIT_NPC_EMOTESTATE) == EMOTE_STATE_DANCE},
             {"following_requester", ai->HasStrategy("follow", BOT_STATE_NON_COMBAT)},
             {"saved_outfit_available", !frame.savedEquipment[target.guid].empty()},
             {"current_combat_role", ai->IsTank(bot)   ? "tank"
                                     : ai->IsHeal(bot) ? "healer"
                                                       : "damage"},
             {"native_facts", std::move(nativeFacts)}});
        // Only surface named, relevant spells. An ordinary 'buff me' does not
        // require transmitting the party's spellbooks or a second classifier call.
        for (auto const& [spellId, known] : bot->GetSpellMap())
        {
            if (known->State == PLAYERSPELL_REMOVED || !known->Active || !(known->specMask & bot->GetActiveSpecMask()))
                continue;
            auto spell = sSpellMgr->GetSpellInfo(spellId);
            if (!spell || spell->IsPassive())
                continue;
            std::string name = spell->SpellName[LOCALE_enUS];
            if (name.empty())
                continue;
            std::string matching = name;
            std::transform(matching.begin(), matching.end(), matching.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            auto colon = matching.find(": ");
            bool named = lower.find(matching) != std::string::npos ||
                         (colon != std::string::npos && lower.find(matching.substr(colon + 2)) != std::string::npos);
            if (!named)
                continue;
            auto key = "ability:" + std::to_string(spellId);
            abilities.emplace(key, name + " " + spell->Rank[LOCALE_enUS] + ", known by " + bot->GetName());
            frame.abilities[target.guid].emplace(key, spellId);
        }
        {
            auto carried = std::move(owned);
            std::map<uint32_t, uint32_t> counts;
            for (auto const& item : carried)
            {
                items.emplace("item:" + std::to_string(item.entry), item.name + " (owned by a listed companion)");
                counts[item.entry] += item.count;
                facts["item_catalog"]["item:" + std::to_string(item.entry)] = item.name;
            }
            auto& inventoryFacts = facts["controlled_companions"].back()["carried_items"];
            inventoryFacts = pbc_json::array();
            for (auto const& [entry, count] : counts)
                inventoryFacts.push_back(pbc_json::array({"item:" + std::to_string(entry), count}));
            auto& wornFacts = facts["controlled_companions"].back()["worn_items"];
            wornFacts = pbc_json::array();
            for (auto const& item : carried)
                if (item.equipped && uint8(item.position) < slotNames.size())
                    wornFacts.push_back(pbc_json::array(
                        {"item:" + std::to_string(item.entry), "equipped:" + slotNames[uint8(item.position)]}));
            frame.inventory.emplace(target.guid, std::move(carried));
        }
    }
    if (frame.actors.empty())
        return frame;
    if (abilities.size() > 254)
    {
        frame.abilities.clear();
        abilities = {{"NONE", "No named spell was requested."},
                     {"CLARIFY",
                      "Too many matching abilities; identify a "
                      "particular companion and spell."}};
    }
    // Speech witnesses exclude hostile creatures and distant party members.
    // Action references need the actual nearby world, including enemies and dead
    // allies.
    std::list<Unit*> nearby;
    VisibleActionTarget check{requester};
    Acore::UnitListSearcher<VisibleActionTarget> searcher(requester, nearby, check);
    Cell::VisitObjects(requester, searcher, 60.0f);
    nearby.push_back(requester);
    std::map<uint64_t, std::string> labels;
    for (auto const& [label, target] : frame.targets)
        labels.emplace(target.guid, label);
    for (auto unit : nearby)
        labels.try_emplace(unit->GetGUID().GetRawValue(), "unit:" + std::to_string(unit->GetGUID().GetRawValue()));
    for (auto unit : nearby)
    {
        auto const& label = labels.at(unit->GetGUID().GetRawValue());
        frame.targets.emplace(label, entity(unit));
        targets.emplace(label, "Visible " + unit->GetName() + ", entry " + std::to_string(unit->GetEntry()));
    }
    facts["targets"] = pbc_json::array();
    if (frame.targets.size() > 200)
    {
        // Never discard matching wolves and then pretend the remaining one is
        // unique.
        frame.targets.clear();
        targets = {{"NONE", "This action does not require a target."},
                   {"CLARIFY",
                    "The vicinity is too crowded to resolve a target "
                    "reliably; ask to move closer."}};
        facts["targets_incomplete"] = true;
    }
    else
        for (auto const& [label, target] : frame.targets)
            if (auto unit = ObjectAccessor::GetUnit(*requester, ObjectGuid(target.guid)))
            {
                targets[label] = unit == requester
                                     ? "The requesting human " + unit->GetName()
                                     : "Visible " + unit->GetName() + ", entry " + std::to_string(unit->GetEntry());
                auto victim = unit->GetVictim();
                if (victim && check(victim) && unit->IsValidAttackTarget(victim))
                {
                    frame.assistTargets[label].push_back(entity(victim));
                    targets[label] += ". Currently fighting " + victim->GetName();
                }
                auto attackers = pbc_json::array();
                for (auto attacker : unit->getAttackers())
                    if (check(attacker) && unit->IsValidAttackTarget(attacker))
                    {
                        frame.protectTargets[label].push_back(entity(attacker));
                        auto guid = attacker->GetGUID().GetRawValue();
                        auto id = labels.contains(guid) ? labels.at(guid) : "unit:" + std::to_string(guid);
                        attackers.push_back(id);
                        targets[label] += ". Under attack by " + attacker->GetName();
                    }
                pbc_json roles = pbc_json::array();
                if (unit->IsVendor())
                    roles.push_back("merchant");
                if (unit->IsInnkeeper())
                    roles.push_back("innkeeper");
                static char const* const creatureTypes[] = {
                    "unknown",  "beast",   "dragonkin",  "demon",       "elemental", "giant",          "undead",
                    "humanoid", "critter", "mechanical", "unspecified", "totem",     "non-combat pet", "gas cloud"};
                auto type = unit->GetCreatureType();
                facts["targets"].push_back(
                    {{"id", label},
                     {"public_roles", roles},
                     {"label", unit->GetName()},
                     {"entry", unit->GetEntry()},
                     {"creature_type", type < std::size(creatureTypes) ? creatureTypes[type] : "unknown"},
                     {"alive", unit->IsAlive()},
                     {"hostile", requester->IsHostileTo(unit)},
                     {"attackable", requester->IsValidAttackTarget(unit)},
                     {"friendly_to_requester", requester->IsFriendlyTo(unit)},
                     {"attackers", attackers},
                     {"is_requester", unit == requester},
                     {"distance_yards", std::round(requester->GetDistance(unit) * 10.0) / 10.0},
                     {"selected_at_input", requester->GetTarget() == unit->GetGUID()},
                     {"attacking", victim && labels.contains(victim->GetGUID().GetRawValue())
                                       ? labels.at(victim->GetGUID().GetRawValue())
                                       : ""}});
            }
    frame.durations = ActionDurations(contribution);
    std::map<std::string, std::string> serviceChoices = {
        {"NONE", "Not a trainer learning request, or a general repair."},
        {"ALL_AVAILABLE", "Learn the currently available offered spells at the selected trainer."},
        {"CLARIFY",
         "The requested trainer ability or vendor item is missing or "
         "ambiguous."}};
    for (auto const& [guid, offer] : frame.supplyOffers)
    {
        auto key = "offer:" + std::to_string(guid);
        serviceChoices.emplace(key, "This companion's still-pending offer of " + std::to_string(offer.quantity) +
                                        " x " + offer.items.front().name +
                                        ". Acceptance opens a normal trade, never auto-accepts it.");
        facts["pending_supply_offers"].push_back({{"actor", "player:" + std::to_string(ObjectGuid(guid).GetCounter())},
                                                  {"option", key},
                                                  {"item", offer.items.front().name},
                                                  {"quantity", offer.quantity}});
    }
    facts["service_offer_fields"] = {"option", "name", "copper", "bundle_units"};
    for (auto const& actor : frame.actors)
        if (auto bot = ObjectAccessor::FindPlayer(ObjectGuid(actor.actor.guid)))
        {
            auto& offers = frame.services[actor.actor.guid];
            auto& visible = facts["service_offers"];
            if (!visible.is_array())
                visible = pbc_json::array();
            for (auto const& [label, target] : frame.targets)
            {
                auto unit = ObjectAccessor::GetUnit(*requester, ObjectGuid(target.guid));
                auto provider = unit ? unit->ToCreature() : nullptr;
                if (!provider)
                    continue;
                pbc_json providerOffers = pbc_json::array();
                for (auto offer : PlayerbotDialogue::ServiceOffers(bot, provider, true, true, true, true))
                {
                    std::string option = ActionName(offer.kind) + ":" + std::to_string(offer.objectId);
                    if (offer.kind != ActionKind::Repair)
                        serviceChoices.emplace(option, offer.name);
                    providerOffers.push_back({option, offer.name, offer.copper, offer.bundleSize});
                    offers.push_back(std::move(offer));
                }
                if (!providerOffers.empty())
                    visible.push_back({{"actor", "player:" + std::to_string(bot->GetGUID().GetCounter())},
                                       {"provider", label},
                                       {"offers", std::move(providerOffers)}});
            }
        }
    for (auto const& actor : frame.actors)
        if (auto bot = ObjectAccessor::FindPlayer(ObjectGuid(actor.actor.guid)))
            for (auto const& [label, target] : frame.targets)
                if (auto unit = ObjectAccessor::GetUnit(*requester, ObjectGuid(target.guid)))
                    for (auto offer : PlayerbotDialogue::QuestOffers(bot, unit))
                    {
                        auto key = "quest:" + std::to_string(offer.id);
                        serviceChoices.emplace(key, offer.name);
                        facts["quest_offers"].push_back(
                            {{"actor", "player:" + std::to_string(bot->GetGUID().GetCounter())},
                             {"provider", label},
                             {"intent", ActionName(offer.kind)},
                             {"option", key},
                             {"name", offer.name}});
                        frame.quests[actor.actor.guid].push_back(std::move(offer));
                    }
    for (auto const& [guid, roles] : frame.roles)
        for (auto const& [role, name] : roles)
        {
            auto key = "role:" + std::to_string(role);
            serviceChoices.emplace(key, "Adopt the native " + name + " tactical role; no talent/spec change.");
            facts["tactical_roles"].push_back({{"actor", "player:" + std::to_string(ObjectGuid(guid).GetCounter())},
                                               {"option", key},
                                               {"role", name}});
        }
    for (auto const& [guid, specs] : frame.specs)
        for (auto const& [index, name] : specs)
        {
            auto key = "spec:" + std::to_string(index);
            serviceChoices.emplace(
                key, index ? "Already unlocked secondary specialization." : "Already unlocked primary specialization.");
            facts["unlocked_specs"].push_back({{"actor", "player:" + std::to_string(ObjectGuid(guid).GetCounter())},
                                               {"option", key},
                                               {"description", name}});
        }
    if (serviceChoices.size() > 254)
    {
        frame.services.clear();
        frame.quests.clear();
        frame.supplyOffers.clear();
        facts["pending_supply_offers"] = pbc_json::array();
        facts["quest_offers"] = pbc_json::array();
        facts["service_offers"] = pbc_json::array();
        serviceChoices = {{"NONE", "No service requested."},
                          {"CLARIFY", "Too many service choices; be more specific."}};
    }
    std::map<std::string, std::string> moneyChoices = {{"NONE", "No spending was requested."},
                                                       {"CLARIFY",
                                                        "Missing or ambiguous explicit spending limit. A price "
                                                        "mentioned in passing is not permission."}};
    bool mentionsEach =
        std::regex_search(lower, std::regex(R"(\b(each|apiece|per (bot|person|companion|character))\b)"));
    for (auto const& [text, copper] : ActionMoneyAmounts(contribution))
    {
        auto key = "total:" + std::to_string(copper);
        frame.moneyLimits[key] = {copper, false};
        moneyChoices[key] = "At most " + text +
                            " TOTAL across recipients and both actions. Explicit "
                            "spending authorization required.";
        if (mentionsEach)
        {
            key = "each:" + std::to_string(copper);
            frame.moneyLimits[key] = {copper, true};
            moneyChoices[key] = "At most " + text +
                                " PER COMPANION, only when the human explicitly "
                                "authorizes each, never when negated.";
        }
    }
    static std::regex const quantityPattern(R"(\b([0-9]+|one|two|three|four|five|six|seven|eight|nine|ten|a|an)\b)");
    static std::map<std::string, uint32_t> const quantityWords = {{"a", 1},     {"an", 1},    {"one", 1},  {"two", 2},
                                                                  {"three", 3}, {"four", 4},  {"five", 5}, {"six", 6},
                                                                  {"seven", 7}, {"eight", 8}, {"nine", 9}, {"ten", 10}};
    for (std::sregex_iterator it(lower.begin(), lower.end(), quantityPattern), end; it != end; ++it)
    {
        auto start = std::size_t(it->position());
        auto finish = start + it->length();
        if ((start && (lower[start - 1] == '.' || lower[start - 1] == '-')) ||
            (finish < lower.size() && lower[finish] == '.' && finish + 1 < lower.size() &&
             std::isdigit(static_cast<unsigned char>(lower[finish + 1]))))
            continue;
        auto text = (*it)[1].str();
        uint32_t count = quantityWords.contains(text) ? quantityWords.at(text)
                         : text.size() <= 6           ? std::stoul(text)
                                                      : 0;
        if (count && count <= 100000)
            frame.quantities.emplace(text, count);
    }
    for (uint32_t count = 1; count <= 20; ++count)
        frame.quantities.emplace("NUM:" + std::to_string(count), count);
    frame.quantities.emplace("PAIR", 2);
    std::map<std::string, std::string> quantities = {
        {"NONE", "No item quantity applies."},
        {"CLARIFY", "An explicit amount is invalid or unsupported."},
        {"UNSPECIFIED", "A plural or bulk request has no amount or identified set. Ask how many, not stock or one."},
        {"PAIR", "The referenced pair or both referenced things, resolved from the exchange."},
        {"ALL_REFERENCED", "The entire identified offered or discussed set."}};
    for (auto const& [text, count] : frame.quantities)
        if (text != "PAIR")
        {
            auto canonical = "NUM:" + std::to_string(count);
            frame.quantities.emplace(canonical, count);
            quantities.emplace(canonical,
                               "Exactly " + std::to_string(count) + " individual units of the requested item.");
        }
    if (items.size() > 254)
    {
        items = {{"NONE", "No inventory item needed."},
                 {"CLARIFY", "Too many carried items; name a more specific item."}};
        facts["inventory_incomplete"] = true;
    }
    std::map<std::string, std::string> durations = {
        {"NONE", "No precise interval was specified. Use the native default until released."},
        {"CLARIFY", "An exact interval was specified, but no listed precise duration matches it."}};
    for (auto const& [text, ms] : frame.durations)
        durations.emplace(text, "The human explicitly requested " + text + " for this intent.");
    std::map<std::string, std::string> intents = {
        {"NONE", "No request for this operation; description, quotation or hypothetical only."},
        {"CLARIFY", "There is a request but its operation is genuinely unspecified or unsupported."},
        {"follow",
         "Accompany the requesting human using the existing layout. A requested change in spacing is formation."},
        {"stay",
         "Remain at the present position without changing layout, until released or for the requested duration."},
        {"keep_order", "Continue an already active native task unchanged, including its original timer."},
        {"approach", "Move to the specified other person or location, distinct from returning to the requester."},
        {"regroup",
         "Return to the requesting human and continue following them. Holding on arrival is a separate operation."},
        {"retreat", "Withdraw from combat toward the requesting human."},
        {"formation",
         "Change the ongoing spatial arrangement or spacing of the group. Its formation option determines whether "
         "to hold or follow. Ordinary movement toward the requester is regroup, not a layout change."},
        {"resume", "Release the existing hold or restrictive order and restore previous movement."},
        {"attack", "Take down or engage the requested creature using native combat."},
        {"pull",
         "A tank should pull the specified enemy with its available "
         "native pull ability and bring it back to the party position."},
        {"assist",
         "Assist the specified friendly party member against their "
         "current attack target."},
        {"protect",
         "Protect the specified friendly party member against their "
         "current attacker; ambiguous attackers require "
         "clarification."},
        {"stop_attack", "Stop attacking and stop initiating combat until a new combat order."},
        {"pet_attack", "Order existing pets to attack the specified enemy."},
        {"pet_follow", "Order existing pets to follow their owner."},
        {"pet_stay", "Order existing pets to stay at their current position."},
        {"pet_passive", "Set existing pets to passive reaction."},
        {"pet_defensive", "Set existing pets to defensive reaction."},
        {"pet_aggressive", "Set existing pets to aggressive reaction."},
        {"buff",
         "Apply a useful class buff to the specified friendly target; "
         "respect a named spell using the ability "
         "question."},
        {"heal",
         "Heal the specified injured friendly target with a known "
         "suitable healing spell."},
        {"cleanse",
         "Remove a dispellable harmful effect from the specified "
         "friendly target."},
        {"resurrect",
         "Offer native resurrection to the specified dead friendly "
         "player; their consent remains native."},
        {"inspect", "Describe the companion inventory or supplies."},
        {"use",
         "Use a specified owned item once, on self unless a target is "
         "explicitly requested."},
        {"equip", "Put an owned item onto the character's body in an equipment slot. Direction: bag to body."},
        {"unequip",
         "Put currently worn gear away in the character's own backpack. Direction: body to bag. "
         "The requested item can be a worn body slot or outfit set. No transfer or destruction."},
        {"restore_equipment",
         "Put back the exact equipment saved by a previous removal. No invented or substituted gear."},
        {"dance", "Request a playful native dance performance; the character may agree, negotiate or decline."},
        {"stop_dance",
         "End an ongoing dance. A prohibition on future dancing does not request stopping an absent performance."},
        {"trade", "Give, lend or hand over requested carried items through native trade consent."},
        {"currency_request",
         "The human asks the companion to give/lend/transfer gold, silver, copper or coins. "
         "Currency gifts are unsupported: select this to let the companion explain rather than inventing "
         "an item transfer or asking for a creature target. Spending money on supported services is different."},
        {"train",
         "Learn explicitly requested available trainer spells for an "
         "account alt bot, with a spending limit."},
        {"repair",
         "Repair carried equipment at a nearby repair NPC using the "
         "bot's money within a stated limit."},
        {"buy",
         "Buy a specified number of units from the selected nearby vendor "
         "with an explicit spending limit."},
        {"role",
         "Adopt a supported tank/healer/damage tactical role. This does "
         "not switch spec, rebuild talents or learn "
         "abilities."},
        {"spec",
         "Switch between already unlocked primary/secondary "
         "specializations. Never unlock dual spec, spend gold or "
         "rebuild talents."},
        {"talents",
         "Explicitly rebuild the active spec's talents using an offered "
         "configured preset, normal trainer and stated spending limit. Never "
         "infer a rebuild from a tactical role or spec switch."},
        {"share_quest",
         "Offer one listed owned quest to the human requester's party through normal sharing. "
         "No recipient is claimed to have accepted it; target is the requesting human."},
        {"accept_quest",
         "Accept one listed offered quest from the identified nearby NPC or party member. "
         "No completion, abandonment or reward choice."},
        {"quest_help",
         "Help the human with one listed current quest through native combat assistance and looting. "
         "Target is the requester. No autonomous travel, instant progress or reward choice."},
        {"invite", "Send a normal party invitation to the specified nearby player; requires native leader authority."},
        {"join",
         "Accept an existing invitation from the specified player. Never create an invitation or leave another group."},
        {"duel",
         "Challenge the requesting human to a duel; their normal client acceptance is required. "
         "No forced fight, third-party challenge or combat attack."},
        {"leave", "Explicitly leave the companion's current party; no target needed."},
        {"decline_offer",
         "Only decline an identified offered transfer, with no replacement transfer requested. A request for different "
         "items is trade."},
        {"cancel", "Explicitly cancel outstanding dialogue orders."}};
    std::map<std::string, std::string> formations = {{"NONE", "Not a formation request."},
                                                     {"CLARIFY", "Requested formation is unclear or unsupported."}};
    for (auto name : {"near", "far", "line", "circle", "arrow", "shield", "queue", "chaos"})
        formations.emplace(name, "Native " + std::string(name) + " formation.");
    formations.emplace(
        "spread", "Spread into reachable positions while FOLLOWING the requester. Keep moving with the requester.");
    formations.emplace("spread_hold",
                       "Spread into reachable WAITING positions and STAY there, including while a tank pulls. "
                       "Other companions must not run out after the puller.");
    if (!frame.supplyOffers.empty())
        intents.emplace("accept_offer", "Accept an identified current native supply offer exactly as offered.");
    frame.questions.push_back({"second_action",
                               "Does the human request a second distinct new operation? Continuing an unchanged "
                               "existing task, quoting an order or explaining a reason does not count.",
                               {{"NONE", "Zero or one new operation."},
                                {"YES", "Exactly two new requested operations."},
                                {"CLARIFY", "More than two operations or an unclear sequence."}}});
    facts["interpretation_instructions"] =
        "Interpret only the current human request. Recent witnessed exchanges, recent_combat_order and current "
        "pending_request/pending_supply_offers may resolve references, omitted verbs, offers or answers to a pending "
        "question. They do not authorize unrelated new operations. Interpret each requested operation separately: a "
        "quoted or negated clause does not cancel a different live instruction. Polite questions and permission to "
        "stop a task can request action. Infer the intended meaning first; availability and native execution are "
        "separate. Do not substitute an available item, target or ability for an unavailable requested one. Each "
        "question reads the same evidence; use only fields relevant to its operation. Current native facts establish "
        "movement and inventory; past promises do not establish completed actions. A short answer can accept a "
        "delivered spoken offer or supply the missing field of the pending request. Distinguish items mentioned from "
        "items offered. An unchanged existing task needs no new execution.";
    for (unsigned slot = 1; slot <= 2; ++slot)
    {
        auto suffix = std::to_string(slot);
        std::string instruction =
            "Follow state.actions.interpretation_instructions. For " +
            std::string(slot == 1 ? "the first new operation: " : "the explicitly requested second new operation: ");
        frame.questions.push_back({"intent" + suffix, instruction + "Identify its meaning.", intents});
        frame.questions.push_back(
            {"recipient" + suffix,
             instruction +
                 "Identify the controlled companion or collective addressed. Use direct address, whisper recipient, "
                 "current exchange and controlled participants. Release permission is still an instruction.",
             recipients});
        frame.questions.push_back(
            {"target" + suffix,
             instruction +
                 "Identify the unit reference belonging to this operation using current selection, witnessed "
                 "references and native spatial facts. Items are not units. Implicit receiving human and movement to "
                 "the requesting human use NONE. Ambiguous required references use CLARIFY. A creature type can mean "
                 "the nearest matching alive attackable creature. Selection, witnessed references, combat "
                 "relationships "
                 "and distinguishing spatial facts can identify a particular unit. When several candidates remain "
                 "indistinguishable, ask. Native attackability establishes feasibility, not hostility. Never "
                 "substitute a target.",
             targets});
        frame.questions.push_back(
            {"reference" + suffix,
             instruction +
                 "What actually identifies this operation's unit reference? Visibility alone does not "
                 "identify a pointer, but a unique relevant native relationship can. Distinguish an unspecified "
                 "pointer from a creature-type request allowing nearest matching selection.",
             {{"NONE", "This operation needs no separate unit reference or uses its implicit human recipient."},
              {"SELECTED", "The current input-time selection identifies the referred unit."},
              {"IDENTIFIED",
               "The human identifies the unit by name, distinguishing spatial or combat relationships, or requests a "
               "matching "
               "creature type with nearest matching selection allowed. A bare demonstrative does not "
               "give that permission."},
              {"RECENT",
               "A witnessed recent exchange or combat order identifies the reference or repeated creature type."},
              {"UNRESOLVED",
               "A required reference remains ambiguous after current selection, witnessed context and relevant "
               "native spatial/combat relationships: several units still fit the human's description."}}});
        frame.questions.push_back({"duration" + suffix,
                                   instruction + "Identify an explicit wait, formation hold or performance duration.",
                                   durations});
        frame.questions.push_back(
            {"formation" + suffix,
             instruction +
                 "Identify its formation and whether spacing accompanies WAITING here or FOLLOWING the requester.",
             formations});
        frame.questions.push_back({"ability" + suffix,
                                   instruction + "For buff/heal/cleanse/resurrect, identify an explicitly "
                                                 "requested known spell. Generic requests need "
                                                 "NONE. "
                                                 "A named spell missing from these choices needs CLARIFY, never an "
                                                 "automatic substitute. "
                                                 "Use the highest listed known rank unless an exact rank was "
                                                 "requested. Other intents need NONE.",
                                   abilities});
        {
            frame.questions.push_back(
                {"item" + suffix,
                 instruction + "Identify the requested item using the current words and witnessed discussion or offer. "
                               "item_catalog resolves names; carried_item_fields and worn_item_fields label the "
                               "compact inventory rows. "
                               "Distinguish carried items from worn body slots or an outfit set. A requested "
                               "body-slot removal uses its equipped slot even without an item name; native "
                               "execution checks whether anything is worn there. Never substitute another "
                               "carried item when the requested one is missing.",
                 items});
            frame.questions.push_back(
                {"quantity" + suffix,
                 instruction +
                     "Identify quantity meaning. A singular request defaults to one; an unspecified plural "
                     "requires an amount question, even if stock is known. Native item names do not establish "
                     "the requested quantity. Pair/set references "
                     "belong to the referenced items or recipients, not invariably two of one item. "
                     "Durations and prices are separate. Judge intent even when stock is insufficient.",
                 quantities});
        }
        {
            frame.questions.push_back(
                {"service" + suffix,
                 instruction + "For training choose ALL_AVAILABLE only for a general training "
                               "request, otherwise the "
                               "exact offered spell. For purchases choose the exact offered "
                               "item. For a tactical role choose "
                               "its role option; for switching existing specs choose its "
                               "unlocked slot. Do not confuse a "
                               "tactical role with a spec switch or talent rebuilding. For an "
                               "explicit talent rebuild choose the "
                               "requested offered preset; missing or ambiguous preset needs "
                               "CLARIFY. Share/accept/help quest needs its listed quest option, never ALL_AVAILABLE. "
                               "Accept/decline a supply offer needs its exact offer option. Repairs need NONE.",
                 serviceChoices});
            frame.questions.push_back({"money" + suffix,
                                       instruction + "Choose the explicitly authorized maximum expense, "
                                                     "total across recipients unless the "
                                                     "human explicitly said each. Missing cap needs "
                                                     "CLARIFY; never infer permission from a "
                                                     "quoted price. Nonspending actions, including "
                                                     "tactical role changes, need NONE.",
                                       moneyChoices});
        }
    }
    frame.factsJson = facts.dump();
    return frame;
}

void BindCombatReference(ActionFrame& frame, RecentCombatOrder const& recent, std::string const&)
{
    // Offer a grounded repeat referent independently of wording. The interpreter
    // must explicitly choose it; merely remembering an old order cannot execute it.
    auto orders = pbc_json::parse(recent.factsJson);
    if (!orders.is_array() || orders.size() != 1)
        return;
    auto const& previous = orders.front();
    auto facts = pbc_json::parse(frame.factsJson);
    std::string nearest;
    double distance = std::numeric_limits<double>::max();
    for (auto const& target : facts["targets"])
    {
        auto id = target.value("id", "");
        auto found = frame.targets.find(id);
        if (found == frame.targets.end() || !target.value("alive", false) || !target.value("attackable", false) ||
            target.value("entry", 0u) != previous.value("target_entry", 0u) ||
            found->second.guid == previous.value("previous_target_guid", uint64_t{0}))
            continue;
        auto yards = target.value("distance_yards", std::numeric_limits<double>::max());
        if (yards < distance)
        {
            distance = yards;
            nearest = id;
        }
    }
    if (nearest.empty())
        nearest = "CLARIFY";
    frame.combatReference = {previous.value("recipient", ""), previous.value("intent", ""), nearest};
    for (auto& question : frame.questions)
        if (question.id == "target1")
            question.criteria.emplace(
                "RECENT_COMBAT",
                "Repeat the preceding " + previous.value("intent", "combat") + " on another " +
                    previous.value("target_name", "matching creature") + ". Bound next target: " +
                    (nearest == "CLARIFY"
                         ? "no fresh matching creature is identified"
                         : std::find_if(facts["targets"].begin(), facts["targets"].end(),
                                        [&](auto const& target) { return target.value("id", "") == nearest; })
                               ->value("label", nearest)) +
                    ". Use this witnessed continuation only when the human repeats that order.");
    // Public/native evidence of the accepted preceding request supplies the
    // referent's identity even when its spoken acknowledgement was interrupted.
    // Acceptance is not a claim that the previous creature died or was killed.
    facts["preceding_accepted_combat_order"] = previous;
    facts["combat_reference_binding"] = {{"recipient", frame.combatReference->recipient},
                                         {"intent", frame.combatReference->intent},
                                         {"target", nearest},
                                         {"policy",
                                          "Nearest fresh matching creature at this human input; "
                                          "use only for the explicitly repeated combat order."}};
    frame.factsJson = facts.dump();
}

}  // namespace PBC
