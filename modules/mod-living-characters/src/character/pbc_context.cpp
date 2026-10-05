// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
#include "pbc_context.h"
#include "pbc_json.h"
#include <algorithm>
#include <map>

namespace PBC
{
std::string BuildSelectionCues(ActorRecord const& actor, std::vector<NoteRecord> const& notes, std::size_t byteLimit)
{
    // Read established facts only. No biography generation or per-message summarization.
    // Explicit corrections take precedence over the older foundation.
    std::string result;
    auto append = [&](std::string const& text)
    {
        if (result.size() < byteLimit)
        {
            auto count = std::min(text.size(), byteLimit - result.size());
            while (count && count < text.size() && (static_cast<unsigned char>(text[count]) & 0xc0) == 0x80)
                --count;
            result.append(text, 0, count);
        }
    };
    for (auto const& note : notes)
        if (note.owner == actor.id && !note.resolved && note.authority != "model")
            append("Authoritative correction/fact: " + note.text + "\n");
    for (auto const& note : notes)
        if (note.owner == actor.id && !note.resolved && note.kind == "commitment" && note.authority == "model")
            append("Personal commitment: " + note.text + "\n");
    if (!actor.summary.empty() || !actor.foundation.empty())
        append("Personal description: " + actor.summary + "\nBackground (subject to corrections): " + actor.foundation);
    return result;
}

std::vector<SelectionState::Observation> BuildSelectionHistory(
    std::vector<ObservationRecord> const& anchorHistory,
    std::map<std::string, std::vector<ObservationRecord>> const& candidateHistory, std::string const& channel,
    std::string const& currentScene)
{
    auto local = [](auto const& value) { return value == "say" || value == "yell" || value == "emote"; };
    std::map<std::string, std::vector<std::string>> witnesses;
    for (auto const& [actor, history] : candidateHistory)
        for (auto const& observation : history)
            witnesses[observation.source.Key()].push_back(actor);
    std::vector<SelectionState::Observation> result;
    for (auto const& observation : anchorHistory)
    {
        if (observation.sceneId == currentScene ||
            (local(channel) ? !local(observation.channel) : observation.channel != channel))
            continue;
        auto found = witnesses.find(observation.source.Key());
        if (found != witnesses.end())
            result.push_back({observation.text, found->second});
    }
    return result;
}

std::vector<SelectionState::Observation> BuildActionReferences(
    std::vector<ObservationRecord> const& anchorHistory,
    std::map<std::string, std::vector<ObservationRecord>> const& candidateHistory, std::string const& channel,
    std::string const& currentScene, uint64_t nowMs)
{
    auto recent = anchorHistory;
    std::erase_if(recent, [&](auto const& observation)
                  { return observation.createdMs > nowMs || nowMs - observation.createdMs >= 120000; });
    std::map<std::string, std::vector<std::string>> witnesses;
    for (auto const& [actor, history] : candidateHistory)
        for (auto const& observation : history)
            witnesses[observation.source.Key()].push_back(actor);
    auto local = [](auto const& value) { return value == "say" || value == "yell" || value == "emote"; };
    std::vector<SelectionState::Observation> result;
    for (auto const& observation : recent)
    {
        if (observation.channel != "action" &&
            (observation.sceneId == currentScene ||
             (local(channel) ? !local(observation.channel) : observation.channel != channel)))
            continue;
        // Preserve chronology: a later spoken offer must not precede an older
        // completed transfer. Both participants must witness the same revision.
        if (auto found = witnesses.find(observation.source.Key()); found != witnesses.end())
            result.push_back({observation.text, found->second});
    }
    return result;
}

namespace
{
std::string CurrentFoundation(ContextInput const& input, pbc_json const& facts)
{
    // Legacy cards remain unchanged in storage. Resolve their current-character
    // placeholders only when assembling this actor's prompt, never as new facts.
    auto value = [&](char const* key)
    {
        if (!facts.contains(key) || facts[key].is_null())
            return std::string("Not supplied");
        return facts[key].is_string() ? facts[key].get<std::string>() : facts[key].dump();
    };
    std::map<std::string, std::string> variables = {{"char_name", input.actor.name},
                                                    {"char_race", value("race")},
                                                    {"char_class", value("class")},
                                                    {"char_gender", value("gender")},
                                                    {"char_level", value("level")},
                                                    {"scene", value("area") + ", " + value("zone")},
                                                    {"char_role", "Use the supplied current class and game facts"},
                                                    {"char_group", "Use current game_facts.group_id and participants"},
                                                    {"char_los", "Use the explicitly supplied current participants"},
                                                    {"memories", "Use personal_notes and recall"},
                                                    {"relationships", "Use personal_notes and recall"},
                                                    {"chat_history", "Use the witnessed observations"},
                                                    {"context", "Use the current game_facts"},
                                                    {"char_gold", value("money")},
                                                    {"pet_info", value("pets")},
                                                    {"equipment", value("equipment")},
                                                    {"combat_status", value("in_combat")}};
    auto foundation = input.actor.foundation;
    for (auto const& [key, replacement] : variables)
    {
        auto token = "{" + key + "}";
        for (std::size_t position = 0; (position = foundation.find(token, position)) != std::string::npos;)
        {
            foundation.replace(position, token.size(), replacement);
            position += replacement.size();
        }
    }
    return foundation;
}
}  // namespace

CharacterContext BuildCharacterContext(ContextInput const& input)
{
    CharacterContext result;
    result.system = input.instructions;
    auto facts = pbc_json::parse(input.gameFactsJson, nullptr, false);
    if (!facts.is_object())
    {
        result.error = "invalid_game_facts";
        return result;
    }
    if (input.task == "dialogue" && !facts.value("action_clarification", std::string{}).empty())
        result.system +=
            "\n\nCurrent reply task: explain the server-supplied action_clarification, asking only for its "
            "missing information. This request has unresolved work; do not promise to execute it or narrate "
            "success. Keep your character's voice and use the current request_interpretation over earlier speech.";
    result.permissions.subjects = input.subjects;
    result.permissions.animations = input.animations;
    for (auto const& [option, description] : input.actionOptions)
        result.permissions.actionOptions.insert(option);
    result.permissions.mayReportToWatch = input.mayReportToWatch;
    result.permissions.reportGroups = input.reportGroups;
    pbc_json context = {{"task", input.task},
                        {"self",
                         {{"id", input.actor.id},
                          {"name", input.actor.name},
                          {"kind", input.actor.kind},
                          {"foundation", CurrentFoundation(input, facts)}}},
                        {"game_facts", facts},
                        {"recall", input.actor.recall},
                        {"current_contribution", input.currentContribution},
                        {"remaining_turns", input.remainingTurns},
                        {"last_allowed_turn", input.remainingTurns <= 1},
                        {"optional_personal_actions", input.actionOptions},
                        {"allowed_subject_ids", input.subjects},
                        {"allowed_animations", input.animations},
                        {"watch_reporting_allowed", input.mayReportToWatch},
                        {"report_groups", input.reportGroups},
                        {"personal_notes", pbc_json::array()},
                        {"group_reports", pbc_json::array()},
                        {"observations", pbc_json::array()}};
    for (auto const& note : input.personalNotes)
    {
        if (note.owner != input.actor.id)
            continue;
        bool protectedNote =
            note.authority != "model" || note.kind == "fact" || (note.kind == "commitment" && !note.resolved);
        if (note.compacted && !protectedNote)
            continue;
        context["personal_notes"].push_back({{"note_id", note.id},
                                             {"version", note.version},
                                             {"subject", note.subject},
                                             {"kind", note.kind},
                                             {"text", note.text},
                                             {"authority", note.authority},
                                             {"resolved", note.resolved}});
    }
    auto reports = input.groupReports;
    reports.insert(reports.end(), input.watchReports.begin(), input.watchReports.end());
    std::stable_sort(reports.begin(), reports.end(), [](auto const& left, auto const& right)
                     { return left.createdMs > right.createdMs; });
    std::size_t reportBytes = 2;
    for (auto const& report : reports)
    {
        if (report.kind != "report" || report.resolved ||
            (!report.owner.empty() && !input.readableGroups.contains(report.owner)) ||
            (!report.subject.empty() && !input.subjects.contains(report.subject)))
            continue;
        pbc_json brief = {{"note_id", report.id}, {"group", report.owner}, {"subject", report.subject},
                          {"text", report.text}, {"created_ms", report.createdMs}, {"reported_by", report.reportedBy},
                          {"access", "Shared report, not personal observation or verified truth."}};
        auto bytes = brief.dump().size() + 1;
        if (reportBytes + bytes > input.reportByteBudget || context["group_reports"].size() >= 8)
            continue;
        reportBytes += bytes;
        context["group_reports"].push_back(std::move(brief));
    }
    // Reserve the output and 2k estimated tokens for the schema, roles and provider framing.
    // Generate() checks the complete serialized request envelope again before reserving money.
    uint64_t fixedTokens = static_cast<uint64_t>(input.outputTokens) + 2048;
    if (input.contextTokens <= fixedTokens)
    {
        result.error = "context_limit_too_small";
        return result;
    }
    uint64_t byteBudget = (input.contextTokens - fixedTokens) * 4;
    // These values become strings inside the chat-completions envelope. Count
    // that escaping too: quotes, backslashes and line breaks in a long history
    // otherwise fit here but overflow Generate()'s serialized request limit.
    uint64_t used = pbc_json(result.system).dump().size() + pbc_json(context.dump()).dump().size();
    if (used >= byteBudget)
    {
        result.error = "identity_and_memory_exceed_context";
        return result;
    }
    std::vector<pbc_json> history;
    for (auto it = input.observations.rbegin(); it != input.observations.rend(); ++it)
    {
        pbc_json observation = {{"source", it->source.Key()}, {"author", it->authorId}, {"channel", it->channel},
                                {"evidence", it->evidence},   {"text", it->text},       {"created_ms", it->createdMs}};
        // The small extra pair of string quotes is conservative; the comma is
        // needed when appending this observation to the context's array.
        uint64_t size = pbc_json(observation.dump()).dump().size() + 1;
        if (used + size > byteBudget)
            break;
        used += size;
        result.permissions.sources.insert(it->source.Key());
        result.includedSources.push_back(it->source);
        history.push_back(std::move(observation));
    }
    if (!input.observations.empty() && history.empty())
    {
        result.error = "latest_observation_exceeds_context";
        return result;
    }
    for (auto it = history.rbegin(); it != history.rend(); ++it)
        context["observations"].push_back(std::move(*it));
    std::reverse(result.includedSources.begin(), result.includedSources.end());
    result.user = context.dump();
    result.success = true;
    return result;
}
}  // namespace PBC
