// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Living Azeroth character response contract. Delivery and persistence remain server-owned.
#ifndef PBC_DIALOGUE_H
#define PBC_DIALOGUE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace PBC
{
struct Segment
{
    enum class Kind { Speech, Emote };
    Kind kind = Kind::Speech;
    std::string text;
    std::string animation;
};

struct NoteProposal
{
    std::string kind;
    std::string text;
    std::string subject;
    std::string scope;
    std::vector<std::string> sources;
};

struct PersonalActionProposal
{
    std::string option; // Server-supplied, actor-specific immutable option.
    int afterSegment = -1; // -1 acts immediately; otherwise require actual delivery.
};

struct Dialogue
{
    std::vector<Segment> segments;
    std::vector<NoteProposal> notes;
    std::size_t rejectedNotes = 0;
    std::vector<PersonalActionProposal> actions;
    std::size_t rejectedActions = 0;
};

struct DialoguePermissions
{
    std::set<std::string> sources; // Immutable observed source IDs with versions, e.g. "42:1".
    std::set<std::string> subjects; // Actor IDs supplied by the server, not display names.
    std::set<std::string> animations; // Native emotes allowed for this actor.
    bool mayReportToWatch = false; // Read old recorded/model responses.
    std::set<std::string> reportGroups;
    uint8_t maxSegments = 6;
    std::set<std::string> actionOptions;
};

// "reply:N" dependencies refer to segment N of THIS response. Such notes are only
// proposals until that segment is actually delivered. Observed source membership
// establishes access/provenance, not the truth of a model interpretation.
std::optional<Dialogue> ParseDialogue(std::string const& text, DialoguePermissions const& permissions);
std::string DialogueSchema(std::set<std::string> const& animations = {}, std::set<std::string> const& actionOptions = {}, uint8_t maxSegments = 6);
}

#endif
