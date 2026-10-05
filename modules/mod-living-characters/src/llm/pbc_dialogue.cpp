// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Living Azeroth changes. Never send unparsed model JSON to game chat.
#include "pbc_dialogue.h"
#include "pbc_json.h"
#include <algorithm>
#include <charconv>
#include <string_view>

namespace PBC
{
namespace
{
bool Keys(pbc_json const& object, std::set<std::string> const& expected)
{
    if (!object.is_object() || object.size() != expected.size())
        return false;
    return std::all_of(expected.begin(), expected.end(), [&](auto const& key) { return object.contains(key); });
}

bool Text(pbc_json const& value, std::size_t limit, bool allowEmpty = false)
{
    if (!value.is_string())
        return false;
    auto const& text = value.get_ref<std::string const&>();
    if ((!allowEmpty && text.empty()) || text.size() > limit)
        return false;
    return std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127 || c == '|'; });
}

bool AllowedSource(std::string const& source, DialoguePermissions const& permissions, std::size_t segments)
{
    if (permissions.sources.contains(source))
        return true;
    if (!source.starts_with("reply:"))
        return false;
    std::size_t index = 0;
    auto parsed = std::from_chars(source.data() + 6, source.data() + source.size(), index);
    return parsed.ec == std::errc() && parsed.ptr == source.data() + source.size() && index < segments;
}
}  // namespace

std::optional<Dialogue> ParseDialogue(std::string const& text, DialoguePermissions const& permissions)
{
    if (text.size() > 65536)
        return std::nullopt;
    auto root = pbc_json::parse(text, nullptr, false);
    if (root.is_discarded() || (!Keys(root, {"segments", "notes"}) && !Keys(root, {"segments", "notes", "actions"})) ||
        !root["segments"].is_array() || root["segments"].size() > std::min<uint8_t>(permissions.maxSegments, 6) ||
        !root["notes"].is_array() || root["notes"].size() > 8)
        return std::nullopt;

    Dialogue result;
    for (auto const& segment : root["segments"])
    {
        if (!Keys(segment, {"kind", "text", "animation"}) || !Text(segment["text"], 255) ||
            !Text(segment["animation"], 32, true) || !segment["kind"].is_string())
            return std::nullopt;
        auto kind = segment["kind"].get<std::string>();
        auto animation = segment["animation"].get<std::string>();
        // The prompt targets 235 bytes, but safe near-limit replies should still
        // reach chat. Private emotes add two asterisks to the native 255-byte limit.
        if ((kind != "speech" && kind != "emote") || (kind == "emote" && !Text(segment["text"], 253)) ||
            (!animation.empty() && (kind != "emote" || !permissions.animations.contains(animation))))
            return std::nullopt;
        result.segments.push_back({kind == "speech" ? Segment::Kind::Speech : Segment::Kind::Emote,
                                   segment["text"].get<std::string>(), animation});
    }

    static std::set<std::string> const kinds = {"memory", "relationship", "commitment", "report", "fact"};
    for (auto const& note : root["notes"])
    {
        ++result.rejectedNotes;
        if (!Keys(note, {"kind", "text", "subject", "scope", "sources"}) || !Text(note["kind"], 24) ||
            !Text(note["text"], 1600) || !Text(note["subject"], 120, true) || !Text(note["scope"], 120) ||
            !note["sources"].is_array() || note["sources"].empty() || note["sources"].size() > 32)
            continue;
        NoteProposal proposal{note["kind"].get<std::string>(),
                              note["text"].get<std::string>(),
                              note["subject"].get<std::string>(),
                              note["scope"].get<std::string>(),
                              {}};
        bool groupReport = proposal.scope.starts_with("group:") &&
                           permissions.reportGroups.contains(proposal.scope.substr(6));
        if (!kinds.contains(proposal.kind) ||
            (!proposal.subject.empty() && !permissions.subjects.contains(proposal.subject)) ||
            (proposal.scope != "personal" && proposal.scope != "watch" && !groupReport) ||
            (groupReport && proposal.kind != "report") ||
            (proposal.scope == "watch" && (!permissions.mayReportToWatch || proposal.kind != "report")))
            continue;
        bool valid = true;
        for (auto const& source : note["sources"])
        {
            if (!Text(source, 96) || !AllowedSource(source.get<std::string>(), permissions, result.segments.size()))
            {
                valid = false;
                break;
            }
            proposal.sources.push_back(source.get<std::string>());
        }
        if (valid)
        {
            result.notes.push_back(std::move(proposal));
            --result.rejectedNotes;
        }
    }
    if (root.contains("actions"))
    {
        // Invalid optional actions never erase otherwise useful character speech.
        auto const& actions = root["actions"];
        if (!actions.is_array() || actions.size() > 2)
            ++result.rejectedActions;
        else
        {
            std::set<std::string> used;
            for (auto const& action : actions)
            {
                ++result.rejectedActions;
                if (!Keys(action, {"option", "after_segment"}) || !Text(action["option"], 120) ||
                    !action["after_segment"].is_number_integer())
                    continue;
                auto option = action["option"].get<std::string>();
                auto after = action["after_segment"].get<int64_t>();
                if (after == -2 && !result.segments.empty())
                    after = static_cast<int64_t>(result.segments.size()) - 1;
                // Explicit abstention carries no native operation or consent.
                // Keep invalid siblings invalid; NONE cannot rescue a bad sequence.
                if (option == "NONE" && after >= -1 && after < static_cast<int64_t>(result.segments.size()))
                {
                    --result.rejectedActions;
                    continue;
                }
                if (!permissions.actionOptions.contains(option) || !used.insert(option).second || after < -1 ||
                    after >= static_cast<int64_t>(result.segments.size()))
                    continue;
                result.actions.push_back({std::move(option), static_cast<int>(after)});
                --result.rejectedActions;
            }
        }
        // A two-operation proposal is one ordered sequence. Wait for all
        // required speech rather than executing a later step ahead of the first.
        if (result.actions.size() == 2)
        {
            int after = std::max(result.actions[0].afterSegment, result.actions[1].afterSegment);
            result.actions[0].afterSegment = result.actions[1].afterSegment = after;
        }
        // These are an ordered sequence. Dropping an invalid first step must
        // never promote its dependent second step into an independent action.
        // Preserve speech and notes, but reject the incomplete sequence.
        if (result.rejectedActions)
            result.actions.clear();
    }
    return result;
}

std::string DialogueSchema(std::set<std::string> const& animations, std::set<std::string> const& actionOptions,
                           uint8_t maxSegments)
{
    auto schema = pbc_json::parse(R"({
      "type":"object","additionalProperties":false,"required":["segments","notes"],
      "properties":{
        "segments":{"type":"array","maxItems":6,"items":{
          "type":"object","additionalProperties":false,"required":["kind","text","animation"],
          "properties":{"kind":{"type":"string","enum":["speech","emote"]},
            "text":{"type":"string","minLength":1,"maxLength":235},"animation":{"type":"string"}}}},
        "notes":{"type":"array","maxItems":8,"items":{
          "type":"object","additionalProperties":false,"required":["kind","text","subject","scope","sources"],
          "properties":{"kind":{"type":"string","enum":["memory","relationship","commitment","report","fact"]},
            "text":{"type":"string","minLength":1,"maxLength":1600},"subject":{"type":"string"},
            "scope":{"type":"string","maxLength":120},
            "sources":{"type":"array","minItems":1,"maxItems":32,"items":{"type":"string"}}}}}
      }})");
    schema["properties"]["segments"]["maxItems"] = std::min<uint8_t>(maxSegments, 6);
    auto speech = schema["properties"]["segments"]["items"];
    speech["properties"]["kind"]["enum"] = {"speech"};
    speech["properties"]["animation"]["enum"] = {""};
    auto emote = speech;
    emote["properties"]["kind"]["enum"] = {"emote"};
    auto& allowed = emote["properties"]["animation"]["enum"];
    for (auto const& animation : animations)
        allowed.push_back(animation);
    schema["properties"]["segments"]["items"] = {{"anyOf", {speech, emote}}};
    if (!actionOptions.empty())
    {
        schema["required"].push_back("actions");
        auto actions = pbc_json::parse(R"({"type":"array","minItems":0,"maxItems":2,
            "description":"Your optional decisions, not a list of capabilities. Use an empty array unless you choose to act. Never propose an option forbidden by your character's established commitments or refused in your speech. Each option occurs at most once.","items":{
            "type":"object","additionalProperties":false,"required":["option","after_segment"],
            "properties":{"option":{"type":"string"},
                "after_segment":{"type":"integer","enum":[-2,-1],
                "description":"-2 waits until your last speech/emote segment is delivered; -1 acts immediately without speech. Use -2 for intentions you speak. Both actions execute as one ordered sequence."}}}})");
        actions["items"]["properties"]["option"]["enum"] = actionOptions;
        actions["items"]["properties"]["option"]["enum"].push_back("NONE");
        actions["items"]["properties"]["option"]["description"] =
            "A chosen native proposal, or NONE to decline it. NONE never executes an action.";
        schema["properties"]["actions"] = std::move(actions);
    }
    return schema.dump();
}
}  // namespace PBC
