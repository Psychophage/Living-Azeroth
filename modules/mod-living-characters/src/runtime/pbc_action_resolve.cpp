// SPDX-License-Identifier: GPL-2.0-or-later
// Living Azeroth: meaning resolves against immutable native snapshots. Capturing
// live game state and executing requests remain separate responsibilities.
#include "pbc_action_input.h"
#include "ObjectGuid.h"
#include <algorithm>
#include <cmath>

namespace PBC
{
std::vector<std::vector<ActionRequest>> ResolveActionDecisions(ActionFrame const& frame, ModelReply const& reply,
                                                               std::string& clarification, ActionFeedback* feedback)
{
    std::map<uint64_t, std::vector<ActionRequest>> grouped;
    bool quoteOnly = false;
    if (!reply.success)
        return {};
    auto answer = [&](std::string const& name) -> std::string
    {
        auto found = reply.decisions.find(name);
        return found == reply.decisions.end() ? "CLARIFY" : found->second;
    };
    if (feedback)
    {
        *feedback = {};
        for (unsigned slot = 1; slot <= 2; ++slot)
        {
            auto suffix = std::to_string(slot);
            if (answer("intent" + suffix) == "NONE" || (slot == 2 && answer("second_action") != "YES"))
                continue;
            auto recipient = answer("recipient" + suffix);
            for (auto const& actor : frame.actors)
            {
                auto id = "player:" + std::to_string(ObjectGuid(actor.actor.guid).GetCounter());
                if (recipient == id || recipient == "ALL")
                    feedback->actors.insert(id);
            }
        }
    }
    auto missing = [&](std::string field)
    {
        if (feedback)
            feedback->missing.insert(std::move(field));
    };
    auto secondAction = answer("second_action");
    if (secondAction == "CLARIFY" || (secondAction == "YES" && answer("intent2") == "NONE"))
    {
        clarification = "Please clarify the requested sequence; I can perform up to two explicit actions.";
        return {};
    }
    for (unsigned slot = 1; slot <= 2; ++slot)
    {
        if (slot == 2 && secondAction == "NONE")
            continue;
        auto suffix = std::to_string(slot);
        auto intent = answer("intent" + suffix);
        if (intent == "keep_order")
            continue;
        if (intent == "NONE")
        {
            if (slot == 1 && secondAction == "YES" && answer("intent2") != "NONE" && answer("intent2") != "keep_order")
                clarification = "Please state the first action before the second one.";
            continue;
        }
        auto recipient = answer("recipient" + suffix);
        auto target = answer("target" + suffix);
        if (slot == 1 && target == "RECENT_COMBAT" && frame.combatReference &&
            intent == frame.combatReference->intent &&
            (recipient == frame.combatReference->recipient || (recipient == "ALL" && frame.actors.size() == 1)))
            target = frame.combatReference->target;
        auto duration = answer("duration" + suffix);
        auto formation = answer("formation" + suffix);
        if (intent == "currency_request")
        {
            clarification =
                "I can't give or lend money through these requests. I can offer an owned item "
                "for you to accept in trade, or help with explicitly authorized purchases and services.";
            if (feedback)
                feedback->status = "unsupported";
            return {};
        }
        bool acceptingOffer = intent == "accept_offer";
        auto kind = acceptingOffer ? std::optional<ActionKind>{ActionKind::Trade} : ParseActionName(intent);
        if (kind == ActionKind::Approach && !frame.actors.empty() &&
            (target == "player:" + std::to_string(ObjectGuid(frame.actors.front().requester.guid).GetCounter()) ||
             (target == "NONE" && answer("reference" + suffix) == "NONE")))
            kind = ActionKind::Regroup;
        bool needsTarget =
            kind && (*kind == ActionKind::Approach || *kind == ActionKind::Attack || *kind == ActionKind::Pull ||
                     *kind == ActionKind::PetAttack || *kind == ActionKind::Assist || *kind == ActionKind::Protect ||
                     (*kind >= ActionKind::Buff && *kind <= ActionKind::Resurrect) ||
                     (*kind == ActionKind::Trade && !acceptingOffer) || *kind == ActionKind::ShareQuest ||
                     *kind == ActionKind::AcceptQuest || *kind == ActionKind::QuestHelp ||
                     *kind == ActionKind::Invite || *kind == ActionKind::Join || *kind == ActionKind::Duel);
        needsTarget =
            needsTarget ||
            (kind && ((*kind >= ActionKind::Train && *kind <= ActionKind::Buy) || *kind == ActionKind::Talents));
        bool requesterTarget = kind && (*kind == ActionKind::Trade || *kind == ActionKind::Duel ||
                                        *kind == ActionKind::QuestHelp || *kind == ActionKind::ShareQuest);
        if (requesterTarget && target == "NONE" && !frame.actors.empty())
            target = "player:" + std::to_string(ObjectGuid(frame.actors.front().requester.guid).GetCounter());
        if (needsTarget && answer("reference" + suffix) == "UNRESOLVED")
            target = "CLARIFY";
        if (kind && needsTarget && !frame.targets.contains(target))
        {
            missing("target" + suffix);
            // Identification evidence changes with the clarified target. Retaining
            // an old UNRESOLVED reference would reject a fresh, selected target.
            missing("reference" + suffix);
            clarification =
                "Which nearby target do you mean? You can name it, ask for the nearest matching "
                "creature, or select it.";
            return {};
        }
        if (!kind || recipient == "NONE" || recipient == "CLARIFY" ||
            (*kind == ActionKind::Stay && duration != "NONE" && !frame.durations.contains(duration)))
        {
            if (!kind)
            {
                missing("intent" + suffix);
                clarification = "Could you clarify what you would like me to do?";
            }
            else if (recipient == "NONE" || recipient == "CLARIFY")
            {
                missing("recipient" + suffix);
                clarification = "Which companion should do that?";
            }
            else
            {
                missing("duration" + suffix);
                clarification = "How long should I wait here?";
            }
            return {};
        }
        uint32_t formationId = 0;
        std::optional<PlayerbotDialogue::Entity> relatedEnemy;
        if (*kind == ActionKind::Assist || *kind == ActionKind::Protect)
        {
            auto const& relationships = *kind == ActionKind::Assist ? frame.assistTargets : frame.protectTargets;
            auto found = relationships.find(target);
            if (found == relationships.end() || found->second.size() != 1)
            {
                clarification =
                    "That ally has no single visible combat target. Please "
                    "identify the enemy to attack.";
                return {};
            }
            relatedEnemy = found->second.front();
        }
        if (*kind == ActionKind::Formation)
        {
            std::vector<std::string> names = {"near",   "far",   "line",  "circle", "arrow",
                                              "shield", "queue", "chaos", "spread", "spread_hold"};
            auto found = std::find(names.begin(), names.end(), formation);
            if (found == names.end())
            {
                missing("formation" + suffix);
                clarification =
                    "Choose a formation: near, far, line, circle, arrow, "
                    "shield, queue or chaos.";
                return {};
            }
            formationId = static_cast<uint32_t>(found - names.begin());
        }
        for (auto request : frame.actors)
        {
            auto actorId = "player:" + std::to_string(ObjectGuid(request.actor.guid).GetCounter());
            if (recipient != "ALL" && recipient != actorId)
                continue;
            request.id += ":" + suffix;
            request.kind = *kind;
            if (needsTarget)
                request.target = frame.targets.at(target);
            if (*kind == ActionKind::Approach && request.target.guid == request.actor.guid)
            {
                clarification = "A companion cannot approach itself. Do you want it to come to you?";
                return {};
            }
            if (relatedEnemy)
            {
                request.subject = request.target;
                request.target = *relatedEnemy;
            }
            request.objectId = formationId;
            if (*kind == ActionKind::Unequip)
            {
                auto choice = answer("item" + suffix);
                auto equipped = frame.equipped.find(request.actor.guid);
                auto mask = frame.equipmentSlots.find(choice);
                if (equipped != frame.equipped.end())
                    for (auto const& item : equipped->second)
                        if ((mask != frame.equipmentSlots.end() && (mask->second & (1u << uint8_t(item.position)))) ||
                            choice == "item:" + std::to_string(item.entry))
                            request.items.push_back(item);
                if (request.items.empty())
                {
                    if (mask != frame.equipmentSlots.end())
                        clarification = "I am not wearing anything in that part of my outfit.";
                    else if (choice.starts_with("item:") || choice == "UNAVAILABLE")
                        clarification = "That item is not currently equipped.";
                    else
                    {
                        clarification = "Which currently worn item or part of my outfit should I take off?";
                        missing("item" + suffix);
                    }
                    return {};
                }
                request.quantity = request.items.size();
            }
            if (*kind == ActionKind::RestoreEquipment)
            {
                auto saved = frame.savedEquipment.find(request.actor.guid);
                if (saved == frame.savedEquipment.end() || saved->second.empty())
                {
                    clarification = "I have no saved outfit from an earlier removal to put back on.";
                    return {};
                }
                request.items = saved->second;
                request.quantity = request.items.size();
            }
            if ((*kind == ActionKind::Dance || *kind == ActionKind::Formation) && frame.durations.contains(duration))
                request.durationMs = frame.durations.at(duration);
            if (acceptingOffer || *kind == ActionKind::DeclineOffer)
            {
                auto found = frame.supplyOffers.find(request.actor.guid);
                if (found == frame.supplyOffers.end() ||
                    answer("service" + suffix) != "offer:" + std::to_string(request.actor.guid))
                {
                    clarification = "Please identify a current supply offer to accept or decline.";
                    return {};
                }
                auto const& offer = found->second;
                request.offerId = offer.id;
                request.target = request.requester;
                request.objectId = offer.objectId;
                request.quantity = offer.quantity;
                request.items = offer.items;
            }
            if (*kind == ActionKind::ShareQuest || *kind == ActionKind::AcceptQuest || *kind == ActionKind::QuestHelp)
            {
                auto choice = answer("service" + suffix);
                auto found = frame.quests.find(request.actor.guid);
                uint32_t quest = 0;
                if (found != frame.quests.end())
                    for (auto const& offer : found->second)
                        if (offer.kind == *kind && choice == "quest:" + std::to_string(offer.id) &&
                            offer.provider.guid ==
                                (*kind == ActionKind::ShareQuest ? request.actor.guid : request.target.guid))
                            quest = offer.id;
                if (!quest || (*kind == ActionKind::ShareQuest && request.target.guid != request.requester.guid))
                {
                    clarification =
                        "Please name an available quest and its nearby giver, or a quest your companion can share with "
                        "you.";
                    return {};
                }
                request.objectId = quest;
            }
            if (*kind == ActionKind::Role)
            {
                auto choice = answer("service" + suffix);
                auto known = frame.roles.find(request.actor.guid);
                uint32_t role = 0;
                if (known != frame.roles.end())
                    for (auto const& [id, name] : known->second)
                        if (choice == "role:" + std::to_string(id))
                            role = id;
                if (!role)
                {
                    clarification =
                        "That companion does not have the requested native "
                        "tactical role available.";
                    return {};
                }
                request.objectId = role;
            }
            if (*kind == ActionKind::Spec)
            {
                auto choice = answer("service" + suffix);
                auto known = frame.specs.find(request.actor.guid);
                std::optional<uint32_t> selected;
                if (known != frame.specs.end())
                    for (auto const& [index, name] : known->second)
                        if (choice == "spec:" + std::to_string(index))
                            selected = index;
                if (!selected)
                {
                    clarification =
                        "That companion does not have the requested "
                        "specialization unlocked and available.";
                    return {};
                }
                request.objectId = *selected;
            }
            if ((*kind >= ActionKind::Train && *kind <= ActionKind::Buy) || *kind == ActionKind::Talents)
            {
                auto money = frame.moneyLimits.find(answer("money" + suffix));
                auto available = frame.services.find(request.actor.guid);
                if (available == frame.services.end())
                {
                    clarification =
                        "Please specify an available nearby service and an "
                        "explicit gold/silver/copper spending limit.";
                    return {};
                }
                quoteOnly |= money == frame.moneyLimits.end();
                if (money == frame.moneyLimits.end())
                    missing("money" + suffix);
                request.copperLimit = money == frame.moneyLimits.end() ? 2147483647 : money->second.first;
                request.moneyBudget = request.groupId + ":money:" + std::to_string(request.copperLimit);
                if (money != frame.moneyLimits.end() && money->second.second)
                    request.moneyBudget += ":" + std::to_string(request.actor.guid);
                auto choice = answer("service" + suffix);
                if (*kind == ActionKind::Talents && (choice == "CLARIFY" || choice == "NONE"))
                {
                    missing("service" + suffix);
                    clarification = "Choose an available talent preset for this trainer: ";
                    bool any = false;
                    for (auto const& offer : available->second)
                        if (offer.kind == ActionKind::Talents && offer.provider.guid == request.target.guid)
                        {
                            clarification += (any ? "; " : "") + offer.name;
                            any = true;
                        }
                    clarification += any ? ". Include a gold/silver/copper spending limit."
                                         : "none are valid for this character and provider.";
                    return {};
                }
                unsigned matches = 0;
                for (auto const& offer : available->second)
                {
                    if (offer.kind != *kind || offer.provider.guid != request.target.guid ||
                        (*kind != ActionKind::Repair && !(choice == "ALL_AVAILABLE" && *kind == ActionKind::Train) &&
                         choice != ActionName(offer.kind) + ":" + std::to_string(offer.objectId)))
                        continue;
                    ++matches;
                    request.reservedCopper += offer.copper;
                    if (*kind == ActionKind::Train)
                        request.trainingSpells.push_back(offer.objectId);
                    else if (*kind == ActionKind::Repair)
                        request.items = frame.inventory.at(request.actor.guid);
                    else if (*kind == ActionKind::Talents)
                    {
                        request.objectId = offer.objectId;
                        request.talentSpec = offer.talentSpec;
                        request.talentPlan = offer.talentPlan;
                    }
                    else
                    {
                        auto count = answer("quantity" + suffix);
                        if (count != "SINGLE" && !frame.quantities.contains(count))
                        {
                            missing("quantity" + suffix);
                            clarification = "Please specify the exact number of individual items to buy.";
                            return {};
                        }
                        request.quantity = count == "SINGLE" ? 1 : frame.quantities.at(count);
                        if (!offer.bundleSize || request.quantity % offer.bundleSize ||
                            request.quantity / offer.bundleSize > 255)
                        {
                            clarification =
                                "That vendor sells bundles; choose an exact "
                                "multiple of the offered bundle size.";
                            return {};
                        }
                        request.objectId = offer.objectId;
                        request.vendorSlot = offer.slot;
                        request.reservedCopper = uint64_t(
                            std::floor(float(uint64_t(offer.baseCopper) * (request.quantity / offer.bundleSize)) *
                                       offer.discount));
                    }
                }
                if (!matches || (*kind != ActionKind::Train && matches != 1) ||
                    request.reservedCopper > request.copperLimit)
                {
                    clarification =
                        "The requested service is unavailable, ambiguous, or "
                        "exceeds the stated spending limit.";
                    return {};
                }
            }
            if (*kind >= ActionKind::Buff && *kind <= ActionKind::Resurrect)
            {
                auto ability = answer("ability" + suffix);
                if (ability != "NONE")
                {
                    auto known = frame.abilities.find(request.actor.guid);
                    if (known == frame.abilities.end() || !known->second.contains(ability))
                    {
                        missing("ability" + suffix);
                        clarification =
                            "That companion does not have the requested "
                            "spell/rank available. Please clarify the "
                            "ability.";
                        return {};
                    }
                    request.objectId = known->second.at(ability);
                }
            }
            if (*kind >= ActionKind::Use && *kind <= ActionKind::Trade && !acceptingOffer)
            {
                auto itemChoice = answer("item" + suffix);
                auto quantity = answer("quantity" + suffix);
                auto inventory = frame.inventory.find(request.actor.guid);
                if (itemChoice == "UNAVAILABLE")
                {
                    clarification = "I don't have that requested item. No substitute has been offered or transferred.";
                    if (feedback)
                        feedback->status = "unavailable";
                    return {};
                }
                auto bundle = reply.itemSets.find(suffix);
                if (*kind == ActionKind::Trade && bundle != reply.itemSets.end())
                {
                    if (inventory == frame.inventory.end() || bundle->second.empty() || bundle->second.size() > 6)
                    {
                        clarification = "I can't offer that exact set of items from my current inventory.";
                        return {};
                    }
                    for (auto const& [entry, count] : bundle->second)
                    {
                        uint32_t remaining = count;
                        if (!remaining || !entry.starts_with("item:"))
                        {
                            clarification = "Please identify the exact items and quantities to transfer.";
                            return {};
                        }
                        for (auto const& item : inventory->second)
                            if (entry == "item:" + std::to_string(item.entry) && item.tradable && !item.equipped &&
                                remaining)
                            {
                                request.tradeQuantities[item.entry] = count;
                                request.items.push_back(item);
                                remaining = remaining > item.count ? remaining - item.count : 0;
                            }
                        if (remaining)
                        {
                            clarification = "That exact set is unavailable in my current tradable inventory.";
                            if (feedback)
                                feedback->status = "unavailable";
                            return {};
                        }
                        request.quantity += count;
                    }
                    if (request.target.guid != request.requester.guid)
                    {
                        clarification = "This inventory trade must be offered to its human requester.";
                        return {};
                    }
                    grouped[request.actor.guid].push_back(std::move(request));
                    continue;
                }
                if (inventory == frame.inventory.end() || !itemChoice.starts_with("item:") ||
                    (quantity != "SINGLE" && !frame.quantities.contains(quantity)))
                {
                    if (inventory == frame.inventory.end() || !itemChoice.starts_with("item:"))
                        missing("item" + suffix);
                    if (quantity != "SINGLE" && !frame.quantities.contains(quantity))
                        missing("quantity" + suffix);
                    if (feedback && feedback->missing.contains("item" + suffix) &&
                        !feedback->missing.contains("quantity" + suffix))
                        clarification = "Which owned item would you like? Nothing has been offered or transferred yet.";
                    else if (feedback && !feedback->missing.contains("item" + suffix))
                        clarification =
                            "How many of that item would you like? Nothing has been offered or transferred yet.";
                    else
                        clarification =
                            "What item would you like, and how many? Nothing has been offered or transferred yet.";
                    return {};
                }
                request.quantity = quantity == "SINGLE" ? 1 : frame.quantities.at(quantity);
                uint64_t remaining = request.quantity;
                for (auto const& item : inventory->second)
                    if (itemChoice == "item:" + std::to_string(item.entry))
                    {
                        if (*kind == ActionKind::Equip && item.equipped)
                            continue;
                        if (*kind == ActionKind::Trade && (!item.tradable || item.equipped))
                            continue;
                        if (remaining)
                        {
                            request.objectId = item.entry;
                            request.items.push_back(item);
                            remaining = remaining > item.count ? remaining - item.count : 0;
                        }
                        else if (*kind == ActionKind::Equip)
                        {
                            clarification =
                                "There is more than one matching equipment item; "
                                "use the explicit item command to "
                                "choose one.";
                            return {};
                        }
                    }
                if (remaining || (*kind != ActionKind::Trade && request.quantity != 1))
                {
                    clarification =
                        "That exact quantity is unavailable; use/equip "
                        "supports one item per action.";
                    return {};
                }
                if (*kind == ActionKind::Trade && request.target.guid != request.requester.guid)
                {
                    clarification = "This inventory trade must be offered to its human requester.";
                    return {};
                }
                if (*kind == ActionKind::Use && target != "NONE")
                {
                    if (!frame.targets.contains(target))
                    {
                        clarification = "Please identify the target for that item.";
                        return {};
                    }
                    request.target = frame.targets.at(target);
                }
            }
            if (*kind == ActionKind::Stay && duration != "NONE")
                request.durationMs = frame.durations.at(duration);
            auto& previous = grouped[request.actor.guid];
            if (!previous.empty() && SameActionIntent(previous.back(), request))
                continue;  // Before quoting/reserving money, not only before native dispatch.
            grouped[request.actor.guid].push_back(std::move(request));
        }
    }
    std::vector<std::vector<ActionRequest>> result;
    if (!clarification.empty())
        return result;
    if (quoteOnly)
    {
        uint64_t cost = 0;
        for (auto const& [actor, requests] : grouped)
            for (auto const& request : requests)
                cost += request.reservedCopper;
        clarification = "Current quoted cost: " + std::to_string(cost) +
                        " copper in total. What explicit spending limit do you authorize? For example, 'up to " +
                        std::to_string(cost) + " copper total'. Nothing has been spent.";
        return {};
    }
    std::map<std::string, std::pair<uint64_t, uint64_t>> totals;
    for (auto const& [actor, requests] : grouped)
        for (auto const& request : requests)
            if (!request.moneyBudget.empty())
            {
                auto& total = totals[request.moneyBudget];
                total.first += request.reservedCopper;
                total.second = request.copperLimit;
                if (total.first > total.second)
                {
                    clarification =
                        "The combined cost exceeds that total spending "
                        "limit. Please choose fewer services or another "
                        "explicit cap.";
                    return {};
                }
            }
    for (auto& [actor, requests] : grouped)
        result.push_back(std::move(requests));
    std::vector<ActionRequest::PreparedCompanion> prepared;
    for (auto const& sequence : result)
        for (auto const& request : sequence)
            if (request.kind == ActionKind::Formation && request.objectId == 9)
                prepared.push_back({request.actor, request.authorityVersion});
    for (auto& sequence : result)
        for (auto& request : sequence)
            if (request.kind == ActionKind::Pull)
            {
                // Native pulling includes return to the initial party position.
                // Explicit same-input preparations additionally form a readiness barrier.
                request.returnToParty = true;
                request.preparedParty = prepared;
            }
    return result;
}
}  // namespace PBC
