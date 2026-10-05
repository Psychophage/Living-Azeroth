// SPDX-License-Identifier: GPL-2.0-or-later
// Living Azeroth: complete-reading alternatives tested against native snapshots.
#include "pbc_interpretation.h"
#include "pbc_actions.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <vector>

namespace PBC
{
namespace
{
std::string Choice(pbc_json const& answers, std::string const& key)
{
    return answers.contains(key) && answers[key].is_object() ? answers[key].value("choice", "") : "";
}

bool Named(std::string text, std::string name)
{
    auto lower = [](unsigned char c) { return std::tolower(c); };
    std::transform(text.begin(), text.end(), text.begin(), lower);
    std::transform(name.begin(), name.end(), name.begin(), lower);
    for (auto pos = text.find(name); !name.empty() && pos != std::string::npos; pos = text.find(name, pos + 1))
        if ((!pos || !std::isalnum(static_cast<unsigned char>(text[pos - 1]))) &&
            (pos + name.size() == text.size() || !std::isalnum(static_cast<unsigned char>(text[pos + name.size()]))))
            return true;
    return false;
}

std::vector<std::string> Ranked(pbc_json const& request, pbc_json const& answers, std::string const& key,
                                std::size_t limit = 3)
{
    if (!request["questions"].contains(key) || !answers.contains(key))
        return {};
    auto const& allowed = request["questions"][key]["criteria"];
    std::vector<std::pair<double, std::string>> ranked;
    auto const& answer = answers[key];
    if (answer.contains("probabilities") && answer["probabilities"].is_object())
        for (auto const& [choice, value] : answer["probabilities"].items())
            if (allowed.contains(choice) && value.is_number())
            {
                double probability = value.get<double>();
                if (std::isfinite(probability) && probability > 0 && probability <= 1)
                    ranked.emplace_back(probability, choice);
            }
    std::sort(ranked.begin(), ranked.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
    std::vector<std::string> result;
    // Retained mass bounds a shortlist; it is never joint confidence. OTHER
    // and direct comparison remain necessary for meanings outside that shortlist.
    double retained = 0;
    for (auto const& [probability, choice] : ranked)
        if (result.size() < limit && retained < 0.95)
        {
            result.push_back(choice);
            retained += probability;
        }
    auto chosen = Choice(answers, key);
    if (allowed.contains(chosen) && std::find(result.begin(), result.end(), chosen) == result.end())
    {
        result.insert(result.begin(), chosen);
        if (result.size() > limit)
            result.resize(limit);
    }
    return result;
}
bool Targeted(std::string const& intent)
{
    return std::set<std::string>{"attack", "approach", "assist", "protect", "buff", "heal", "duel"}.contains(intent);
}

}  // namespace

bool NeedsCompleteReading(pbc_json const& request, pbc_json const& answers)
{
    if (!request["state"].contains("actions") || !answers.contains("intent1") ||
        !request["state"]["actions"].contains("controlled_companions") ||
        request["state"]["actions"]["controlled_companions"].empty())
        return false;
    // Literal/recorded contracts without distributions contain no evidence
    // for a probabilistic shortlist. Keep their validated direct interpretation.
    if (!answers["intent1"].contains("probabilities"))
        return false;
    // Gameplay choices are coupled, even when their separate answers are confident.
    // Compare complete meanings before every new operation. Confident conversation
    // can finish in one call; uncertain absence still receives a complete reading.
    auto intent = Choice(answers, "intent1");
    auto second = Choice(answers, "intent2");
    if ((!intent.empty() && intent != "NONE" && intent != "keep_order") || Choice(answers, "second_action") == "YES" ||
        (!second.empty() && second != "NONE" && second != "keep_order" && second != intent))
        return true;
    auto const& first = answers["intent1"];
    if (first.contains("confidence") && first["confidence"].is_number())
    {
        double confidence = first["confidence"].get<double>();
        return std::isfinite(confidence) && confidence >= 0 && confidence < 0.65;
    }
    return false;
}

std::map<std::string, InterpretationReading> CompleteReadings(pbc_json const& request, pbc_json const& answers)
{
    using Choices = std::map<std::string, std::string>;
    Choices base;
    for (auto const& [key, answer] : answers.items())
        if (key != "speaker" && answer.is_object() && answer.contains("choice") && answer["choice"].is_string())
            base.emplace(key, answer["choice"].get<std::string>());
    auto const& questions = request["questions"];
    std::map<std::string, pbc_json> actors;
    for (auto const& actor : request["state"]["actions"].value("controlled_companions", pbc_json::array()))
        actors.emplace(actor.at("id").get<std::string>(), actor);
    auto label = [&](std::string const& id)
    {
        return id == "ALL"           ? std::string("all controlled companions")
               : actors.contains(id) ? actors.at(id).value("label", id)
                                     : id;
    };
    auto fields = [&](std::string const& intent)
    {
        std::vector<std::string> result;
        if (intent == "stay" || intent == "dance" || intent == "formation")
            result.push_back("duration");
        if (intent == "formation")
            result.push_back("formation");
        if (Targeted(intent) ||
            std::set<std::string>{"pull", "pet_attack", "cleanse", "resurrect", "invite", "join", "use", "train",
                                  "repair", "buy", "talents", "share_quest", "accept_quest", "quest_help"}
                .contains(intent))
        {
            result.push_back("target");
            result.push_back("reference");
        }
        if (std::set<std::string>{"buff", "heal", "cleanse", "resurrect"}.contains(intent))
            result.push_back("ability");
        if (std::set<std::string>{"equip", "unequip", "use", "trade"}.contains(intent))
            result.push_back("item");
        if (intent == "trade" || intent == "buy" || intent == "equip" || intent == "use")
            result.push_back("quantity");
        if (std::set<std::string>{"accept_offer", "decline_offer", "train", "repair", "buy", "talents", "role", "spec",
                                  "share_quest", "accept_quest", "quest_help"}
                .contains(intent))
            result.push_back("service");
        if (std::set<std::string>{"train", "repair", "buy", "talents"}.contains(intent))
            result.push_back("money");
        return result;
    };
    auto canonical = [&](Choices choices)
    {
        bool two = choices["second_action"] == "YES";
        for (unsigned slot = 1; slot <= 2; ++slot)
        {
            auto suffix = std::to_string(slot);
            auto intent = choices["intent" + suffix];
            if (intent == "approach" &&
                (choices["target" + suffix] == request["state"]["actions"].value("requester", "") ||
                 (choices["target" + suffix] == "NONE" && choices["reference" + suffix] == "NONE")))
                intent = choices["intent" + suffix] = "regroup";
            std::set<std::string> relevant = {"intent", "recipient"};
            for (auto const& field : fields(intent))
                relevant.insert(field);
            for (auto const& [key, question] : questions.items())
                if (key.ends_with(suffix) && ((slot == 2 && !two) || !relevant.contains(key.substr(0, key.size() - 1))))
                    choices[key] = "NONE";
        }
        return choices;
    };
    std::map<std::string, InterpretationReading> result;
    auto add = [&](Choices choices, std::map<std::string, uint32_t> items = {}, std::string continuation = "")
    {
        if (actors.size() == 1)
            for (auto const& recipient : {"recipient1", "recipient2"})
                if (choices[recipient] == "ALL")
                    choices[recipient] = actors.begin()->first;
        // Two item types for one recipient are one consenting native transfer,
        // not two trade windows. Preserve exact amounts; unknown amounts remain
        // clarification alternatives instead of being guessed from stock.
        if (items.empty() && choices["second_action"] == "YES" && choices["intent1"] == "trade" &&
            choices["intent2"] == "trade" && choices["recipient1"] == choices["recipient2"] &&
            choices["item1"].starts_with("item:") && choices["item2"].starts_with("item:"))
        {
            auto count = [&](std::string const& value)
            {
                if (value == "PAIR")
                    return choices["item1"] == choices["item2"] ? 2u : 1u;
                if (!value.starts_with("NUM:"))
                    return 0u;
                auto digits = value.substr(4);
                if (digits.empty() || digits.size() > 3 ||
                    !std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c); }))
                    return 0u;
                return static_cast<unsigned>(std::stoul(digits));
            };
            auto first = count(choices["quantity1"]), second = count(choices["quantity2"]);
            if (first && second)
            {
                items[choices["item1"]] += first;
                items[choices["item2"]] += second;
                choices["second_action"] = choices["intent2"] = "NONE";
            }
        }
        choices = canonical(std::move(choices));
        for (auto const& suffix : {"1", "2"})
            if (choices[std::string("reference") + suffix] == "UNRESOLVED")
                choices[std::string("target") + suffix] = "CLARIFY";
        // Evidence labels describe how the same concrete object was identified;
        // they are not separate competing meanings. Keep one menu entry per reading
        // so duplicate labels cannot split or inflate its provider probability.
        auto meaningKey = [](Choices value)
        {
            value.erase("reference1");
            value.erase("reference2");
            return value;
        };
        if (result.size() >= 96 ||
            std::any_of(
                result.begin(), result.end(), [&](auto const& entry)
                { return meaningKey(entry.second.choices) == meaningKey(choices) && entry.second.items == items; }))
            return;
        pbc_json steps = pbc_json::array();
        for (unsigned slot = 1; slot <= (choices["second_action"] == "YES" ? 2u : 1u); ++slot)
        {
            auto suffix = std::to_string(slot);
            auto intent = choices["intent" + suffix];
            if (intent == "NONE" || intent.empty())
                continue;
            auto intentKey = "intent" + suffix;
            // Some isolated fixtures omit a second intent question; production supplies both.
            auto intentMeaning = questions.contains(intentKey) ? questions[intentKey]["criteria"].value(intent, intent)
                                                               : questions["intent1"]["criteria"].value(intent, intent);
            pbc_json step = {
                {"recipient", label(choices["recipient" + suffix])}, {"operation", intent}, {"meaning", intentMeaning}};
            if (intent == "regroup")
                step["meaning"] = "Return to the requesting human and rejoin FOLLOWING them.";
            if (intent == "assist" || intent == "protect")
            {
                auto const& targets = request["state"]["actions"].value("targets", pbc_json::array());
                auto subject = std::find_if(targets.begin(), targets.end(), [&](auto const& target)
                                            { return target.value("id", "") == choices["target" + suffix]; });
                if (subject != targets.end())
                {
                    auto name = subject->value("label", choices["target" + suffix]);
                    step["meaning"] = intent == "assist" ? "Help " + name + " fight their current opponent."
                                                         : "Defend " + name + " against their current attackers.";
                    step["subject_is_friendly_to_requester"] = subject->value("friendly_to_requester", false);
                    step["native_requires_friendly_subject"] = true;
                    auto enemies =
                        intent == "protect" ? subject->value("attackers", pbc_json::array()) : pbc_json::array();
                    if (intent == "assist" && !subject->value("attacking", "").empty())
                        enemies.push_back(subject->at("attacking"));
                    step["would_engage"] = pbc_json::array();
                    for (auto const& enemy : enemies)
                    {
                        auto found = std::find_if(targets.begin(), targets.end(), [&](auto const& target)
                                                  { return target.value("id", "") == enemy.get<std::string>(); });
                        step["would_engage"].push_back(
                            found == targets.end() ? enemy : pbc_json(found->value("label", enemy.get<std::string>())));
                    }
                    // Keep unavailable meanings in the menu. These native facts
                    // explain each reading rather than redirecting its subject.
                }
            }
            if (intent == "keep_order")
            {
                step["new_operation"] = false;
                step["current_tasks_to_preserve"] = pbc_json::array();
                for (auto const& [id, actor] : actors)
                    if (choices["recipient" + suffix] == id || choices["recipient" + suffix] == "ALL")
                        step["current_tasks_to_preserve"].push_back(
                            {{"actor", label(id)},
                             {"movement", actor.value("holding_position", false) ? "HOLDING position"
                                          : actor.value("following_requester", false)
                                              ? "FOLLOWING the requester"
                                              : "No hold or follow established"},
                             {"dancing", actor.value("dancing", false)}});
            }
            if (slot == 2)
            {
                bool overlap = choices["recipient1"] == "ALL" || choices["recipient2"] == "ALL" ||
                               choices["recipient1"] == choices["recipient2"];
                step["when"] = overlap
                                   ? "For each recipient, after their first operation completes, including its wait."
                                   : "Independently of the other companion's operation; both may begin now.";
            }
            if (intent == "stay")
                step["effect"] =
                    "Start a NEW hold, replacing any previous deadline. This is not continuing the old hold.";
            for (auto const& field : fields(intent))
            {
                auto key = field + suffix;
                auto value = choices.contains(key) ? choices.at(key) : "NONE";
                if (!value.empty() && value != "NONE" && questions.contains(key))
                    step[field] = value + ": " + questions[key]["criteria"].value(value, value);
                else if (field == "duration")
                    step[field] = "Native default duration: no precise interval was specified.";
            }
            if (slot == 1 && !items.empty())
            {
                step.erase("item");
                step.erase("quantity");
                step["bundle"] = pbc_json::array();
                for (auto const& [item, amount] : items)
                    step["bundle"].push_back(
                        {{"item", questions["item1"]["criteria"].value(item, item)}, {"units", amount}});
            }
            steps.push_back(std::move(step));
        }
        std::string description =
            steps.empty()
                ? "Conversation only: no directive to a controlled companion. A description, reported request, "
                  "quotation or hypothetical is not an order. Preserve existing behaviour and timers."
                : "The complete request: " + steps.dump();
        if (!steps.empty())
        {
            std::vector<std::string> unchanged;
            for (auto const& [id, actor] : actors)
                if (actor.value("holding_position", false) && choices["recipient1"] != id &&
                    choices["recipient1"] != "ALL" &&
                    (choices["second_action"] != "YES" ||
                     (choices["recipient2"] != id && choices["recipient2"] != "ALL")))
                    unchanged.push_back(label(id));
            if (!unchanged.empty())
                description +=
                    " These existing holds and deadlines continue UNCHANGED: " + pbc_json(unchanged).dump() + ".";
        }
        if (!continuation.empty())
            description += " Continue the existing hold and its timer unchanged for " + continuation + ".";
        result.emplace("READING:" + std::to_string(result.size()),
                       InterpretationReading{std::move(choices), std::move(items), std::move(description)});
    };
    auto noAction = base;
    noAction["intent1"] = noAction["intent2"] = noAction["second_action"] = "NONE";
    add(noAction);
    auto recipients = [&](std::string const& key)
    {
        auto list = Ranked(request, answers, key, 3);
        for (auto const& [id, actor] : actors)
            if (Named(request["state"].value("contribution", ""), actor.value("label", "")) &&
                std::find(list.begin(), list.end(), id) == list.end())
                list.push_back(id);
        std::erase_if(list, [&](auto const& id) { return id != "ALL" && !actors.contains(id); });
        if (list.empty() && actors.size() == 1)
            list.push_back(actors.begin()->first);
        if (list.size() > 6)
            list.resize(6);
        return list;
    };
    auto intents = Ranked(request, answers, "intent1");
    if (std::find(intents.begin(), intents.end(), "approach") != intents.end() &&
        questions["intent1"]["criteria"].contains("attack") &&
        std::find(intents.begin(), intents.end(), "attack") == intents.end())
        intents.push_back("attack");
    // Returning to the human is a semantic alternative for movement readings,
    // even if isolated field ranking preferred a layout or a generic approach.
    if (questions["intent1"]["criteria"].contains("regroup") &&
        std::find(intents.begin(), intents.end(), "regroup") == intents.end() &&
        std::any_of(intents.begin(), intents.end(),
                    [](auto const& intent)
                    {
                        auto kind = ParseActionName(intent);
                        return kind && ChangesMovement(*kind);
                    }))
        intents.push_back("regroup");
    auto secondShapes = Ranked(request, answers, "second_action");
    bool allowSequence = !answers.contains("second_action") || !answers["second_action"].contains("probabilities") ||
                         std::find(secondShapes.begin(), secondShapes.end(), "YES") != secondShapes.end();
    std::vector<Choices> singles, sequences;
    for (auto const& intent : intents)
        if (intent != "NONE" && intent != "CLARIFY")
            for (auto const& recipient : recipients("recipient1"))
            {
                auto single = base;
                single["intent1"] = intent;
                single["recipient1"] = recipient;
                single["intent2"] = single["second_action"] = "NONE";
                singles.push_back(single);
                if (!allowSequence)
                    continue;
                for (auto const& second : Ranked(request, answers, "intent2", 2))
                    if (second != "NONE" && second != "CLARIFY" && second != "keep_order" && intent != "keep_order")
                        for (auto const& secondRecipient : recipients("recipient2"))
                        {
                            auto sequence = single;
                            sequence["intent2"] = second;
                            sequence["recipient2"] = secondRecipient;
                            sequence["second_action"] = "YES";
                            sequences.push_back(std::move(sequence));
                        }
            }
    // Seed BOTH shapes before combinatorial detail. A large inventory must not
    // consume the whole menu before any compound operation can be considered.
    std::vector<Choices> plans;
    for (std::size_t i = 0; i < std::max(singles.size(), sequences.size()) && plans.size() < 48; ++i)
    {
        if (i < singles.size())
            plans.push_back(singles[i]);
        if (i < sequences.size() && plans.size() < 48)
            plans.push_back(sequences[i]);
    }
    std::vector<std::vector<InterpretationReading>> alternatives;
    for (auto const& plan : plans)
    {
        add(plan);
        std::vector<InterpretationReading> variants;
        for (unsigned slot = 1; slot <= (plan.at("second_action") == "YES" ? 2u : 1u); ++slot)
        {
            auto suffix = std::to_string(slot);
            for (auto const& field : fields(plan.at("intent" + suffix)))
            {
                auto key = field + suffix;
                auto values = Ranked(request, answers, key);
                // Small discrete domains fit completely; zero shortlist mass does
                // not prove that a layout, duration or other typed argument is absent.
                // Larger inventories/target populations still use the ranked shortlist.
                if (questions.contains(key) && questions[key]["criteria"].size() <= 16)
                    for (auto const& [value, meaning] : questions[key]["criteria"].items())
                        if (value != "CLARIFY" && value != "UNAVAILABLE" &&
                            std::find(values.begin(), values.end(), value) == values.end())
                            values.push_back(value);
                for (auto const& value : values)
                {
                    auto variant = plan;
                    variant[key] = value;
                    // Target and its identifying evidence are one coupled reading.
                    // Changing only the target would retain an old UNRESOLVED label
                    // and erase every otherwise grounded target alternative.
                    if (field == "target")
                    {
                        auto reference = "reference" + suffix;
                        if (value == "RECENT_COMBAT")
                            variant[reference] = "RECENT";
                        else if (value == "CLARIFY")
                            variant[reference] = "UNRESOLVED";
                        else if (value != "NONE" && value != "UNAVAILABLE")
                        {
                            variant[reference] = "IDENTIFIED";
                            for (auto const& target : request["state"]["actions"].value("targets", pbc_json::array()))
                                if (target.value("id", "") == value && target.value("selected_at_input", false))
                                    variant[reference] = "SELECTED";
                        }
                    }
                    variants.push_back({std::move(variant), {}, {}});
                }
                if ((field == "target" || field == "item") && questions.contains(key))
                    for (auto fallback : {"CLARIFY", "UNAVAILABLE"})
                        if (questions[key]["criteria"].contains(fallback))
                        {
                            auto variant = plan;
                            variant[key] = fallback;
                            variants.push_back({std::move(variant), {}, {}});
                        }
            }
        }
        // Continuing an existing hold is an ALTERNATIVE, not an assumption about
        // absent duration. Never elide slot 2 using state preceding slot 1.
        if (plan.at("intent1") == "stay" && questions["intent1"]["criteria"].contains("keep_order") &&
            (!plan.contains("duration1") || plan.at("duration1").empty() || plan.at("duration1") == "NONE"))
        {
            auto const& recipient = plan.at("recipient1");
            auto held = [](auto const& actor)
            { return actor.value("holding_position", false) && !actor.value("dancing", false); };
            bool unchanged = recipient == "ALL"
                                 ? !actors.empty() && std::all_of(actors.begin(), actors.end(),
                                                                  [&](auto const& actor) { return held(actor.second); })
                                 : actors.contains(recipient) && held(actors.at(recipient));
            if (unchanged && plan.at("second_action") == "YES")
            {
                auto secondKind = ParseActionName(plan.at("intent2"));
                bool overlap =
                    recipient == "ALL" || plan.at("recipient2") == "ALL" || recipient == plan.at("recipient2");
                if (overlap && secondKind && ChangesMovement(*secondKind))
                    unchanged = false;
            }
            if (unchanged)
            {
                auto variant = plan;
                if (variant["second_action"] == "YES")
                {
                    for (auto field : {"intent", "recipient", "target", "reference", "duration", "formation", "ability",
                                       "item", "quantity", "service", "money"})
                        std::swap(variant[std::string(field) + "1"], variant[std::string(field) + "2"]);
                    variant["intent2"] = variant["second_action"] = "NONE";
                }
                else
                    variant["intent1"] = "keep_order";
                add(variant, {}, label(recipient));
            }
        }
        if (plan.at("intent1") == "trade")
        {
            auto items = Ranked(request, answers, "item1", 5);
            for (auto const& item : Ranked(request, answers, "item2"))
                if (std::find(items.begin(), items.end(), item) == items.end())
                    items.push_back(item);
            std::erase_if(items, [](auto const& item) { return !item.starts_with("item:"); });
            if (items.size() > 6)
                items.resize(6);
            for (std::size_t a = 0; a < items.size(); ++a)
                for (std::size_t b = a + 1; b < items.size(); ++b)
                {
                    auto variant = plan;
                    variant["quantity1"] = "PAIR";
                    variants.insert(variants.begin(), {std::move(variant), {{items[a], 1}, {items[b], 1}}, {}});
                }
        }
        alternatives.push_back(std::move(variants));
    }
    // Round-robin field alternatives preserve recipient/sequence diversity.
    for (std::size_t round = 0; result.size() < 96; ++round)
    {
        bool any = false;
        for (auto const& list : alternatives)
            if (round < list.size())
            {
                any = true;
                add(list[round].choices, list[round].items);
            }
        if (!any)
            break;
    }
    result.emplace("OTHER", InterpretationReading{
                                {},
                                {},
                                "The complete intended meaning is absent or ambiguous. Ask for the missing "
                                "detail; do not choose a substitute or an incorrect operation. Missing possessions, "
                                "capabilities or execution prerequisites do not make the intended meaning "
                                "ambiguous; choose the understood request even when it cannot be fulfilled."});
    return result;
}

bool AmbiguousCompleteReading(std::map<std::string, InterpretationReading> const& readings, pbc_json const& answer,
                              pbc_json const& nativeState)
{
    auto selected = answer.value("choice", "OTHER");
    if (selected == "OTHER" || !answer.contains("probabilities") || !answer["probabilities"].is_object())
        return false;
    auto const& selectedChoices = readings.at(selected).choices;
    if (selectedChoices.at("second_action") != "YES" &&
        (selectedChoices.at("intent1") == "NONE" || selectedChoices.at("intent1") == "keep_order"))
        return false;  // Uncertain conversation needs no automatic gameplay or forced clarification.
    if (std::any_of(
            selectedChoices.begin(), selectedChoices.end(), [](auto const& field)
            { return field.second == "CLARIFY" || field.second == "UNAVAILABLE" || field.second == "UNRESOLVED"; }))
        return false;  // Preserve the known intent and missing argument for a useful follow-up.
    auto const& probabilities = answer["probabilities"];
    auto probability = [&](std::string const& id)
    {
        if (!probabilities.contains(id) || !probabilities[id].is_number())
            return 0.0;
        double value = probabilities[id].get<double>();
        return std::isfinite(value) && value >= 0 && value <= 1 ? value : 0.0;
    };
    auto effect = [&](InterpretationReading const& reading)
    {
        std::map<std::string, std::vector<pbc_json>> operations;
        auto const actors = nativeState.value("controlled_companions", pbc_json::array());
        auto const& choices = reading.choices;
        for (unsigned slot = 1; slot <= (choices.at("second_action") == "YES" ? 2u : 1u); ++slot)
        {
            auto suffix = std::to_string(slot);
            auto intent = choices.at("intent" + suffix);
            if (intent == "NONE" || intent == "keep_order")
                continue;
            pbc_json operation = {{"intent", intent}};
            for (auto const& [key, value] : choices)
                if (key.ends_with(suffix) && key != "recipient" + suffix && key != "reference" + suffix &&
                    key != "intent" + suffix && !value.empty() && value != "NONE")
                    operation[key.substr(0, key.size() - 1)] = value;
            // Resolve fixed quantity aliases exactly as the native frame does.
            // A mixed-item bundle remains a distinct map of entries and counts.
            if (operation.value("quantity", "") == "PAIR")
                operation["quantity"] = "NUM:2";
            else if (operation.value("quantity", "") == "SINGLE")
                operation["quantity"] = "NUM:1";
            if (slot == 1 && !reading.items.empty())
            {
                operation.erase("item");
                operation.erase("quantity");
                operation["bundle"] = reading.items;
            }
            auto const& targets = nativeState.value("targets", pbc_json::array());
            auto targetId = operation.value("target", "");
            if (slot == 1 && targetId == "RECENT_COMBAT" && nativeState.contains("combat_reference_binding"))
            {
                auto const& binding = nativeState["combat_reference_binding"];
                auto recipient = choices.at("recipient" + suffix);
                if (intent == binding.value("intent", "") &&
                    (recipient == binding.value("recipient", "") || (recipient == "ALL" && actors.size() == 1)))
                    operation["target"] = binding.value("target", targetId);
            }
            if (intent == "assist" || intent == "protect")
                for (auto const& subject : targets)
                    if (subject.value("id", "") == targetId && subject.value("friendly_to_requester", false))
                    {
                        auto enemy = intent == "assist" ? subject.value("attacking", "") : std::string();
                        if (intent == "protect" && subject.contains("attackers") && subject["attackers"].size() == 1)
                            enemy = subject["attackers"][0].get<std::string>();
                        if (std::any_of(targets.begin(), targets.end(),
                                        [&](auto const& candidate)
                                        {
                                            return candidate.value("id", "") == enemy &&
                                                   candidate.value("alive", false) &&
                                                   candidate.value("attackable", false);
                                        }))
                        {
                            operation["intent"] = "attack";
                            operation["target"] = enemy;
                        }
                    }
            for (auto const& actor : actors)
                if (choices.at("recipient" + suffix) == actor.value("id", "") ||
                    choices.at("recipient" + suffix) == "ALL")
                    operations[actor.value("id", "")].push_back(operation);
        }
        for (auto const& actor : actors)
        {
            auto id = actor.value("id", "");
            auto found = operations.find(id);
            if (found == operations.end() || found->second.empty())
                continue;
            auto& sequence = found->second;
            // Terminal regroup and follow have the same ongoing native effect.
            // Before another operation, arrival matters: never collapse regroup
            // followed by a hold into follow followed by an immediate hold.
            if (sequence.back()["intent"] == "regroup")
                sequence.back()["intent"] = "follow";
            // Issuing follow can cancel outstanding gameplay even when already
            // following. Only an explicitly idle native snapshot is redundant.
            if (sequence.size() == 1 && sequence.front()["intent"] == "follow" &&
                actor.value("following_requester", false) && !actor.value("pending_gameplay", true))
                operations.erase(found);
        }
        return operations;
    };
    auto selectedEffect = effect(readings.at(selected));
    double alternative = 0;
    for (auto const& [id, reading] : readings)
        if (id == "OTHER" || effect(reading) != selectedEffect)
            alternative = std::max(alternative, probability(id));
    // Compare the selected complete reading with the strongest different effect.
    // Never add overlapping readings into an inflated confidence score. This
    // separation is an abstention policy; native validation authorizes execution.
    return probability(selected) - alternative + 1e-9 < 0.10;
}
}  // namespace PBC
