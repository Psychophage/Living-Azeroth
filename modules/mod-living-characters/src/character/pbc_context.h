// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
#ifndef PBC_CONTEXT_H
#define PBC_CONTEXT_H

#include "pbc_store.h"
#include "pbc_dialogue.h"
#include "pbc_model.h"
#include <map>
#include <set>

namespace PBC
{
struct ContextInput
{
    ActorRecord actor;
    std::string instructions;
    std::string task = "dialogue";
    std::string gameFactsJson = "{}";
    std::string currentContribution;
    std::vector<ObservationRecord> observations;
    std::vector<NoteRecord> personalNotes;
    std::vector<NoteRecord> watchReports; // Legacy input, rendered through group_reports.
    std::vector<NoteRecord> groupReports;
    std::set<std::string> reportGroups;
    std::set<std::string> readableGroups;
    std::size_t reportByteBudget = 2400;
    std::set<std::string> subjects;
    std::set<std::string> animations;
    std::map<std::string, std::string> actionOptions;
    bool mayReportToWatch = false;
    uint8_t remainingTurns = 1;
    uint32_t contextTokens = 32000;
    uint32_t outputTokens = 2048;
};

struct CharacterContext
{
    bool success = false;
    std::string error;
    std::string system;
    std::string user;
    DialoguePermissions permissions;
    std::vector<SourceVersion> includedSources;
};

// One context policy for both spontaneous and directly requested dialogue.
// Metadata/identity/corrections are retained; recent raw history fills the remaining space.
CharacterContext BuildCharacterContext(ContextInput const& input);
std::string BuildSelectionCues(ActorRecord const& actor, std::vector<NoteRecord> const& notes,
                               std::size_t byteLimit = 1800);
std::vector<SelectionState::Observation> BuildSelectionHistory(
    std::vector<ObservationRecord> const& anchorHistory,
    std::map<std::string, std::vector<ObservationRecord>> const& candidateHistory, std::string const& channel,
    std::string const& currentScene);
std::vector<SelectionState::Observation> BuildActionReferences(
    std::vector<ObservationRecord> const& anchorHistory,
    std::map<std::string, std::vector<ObservationRecord>> const& candidateHistory, std::string const& channel,
    std::string const& currentScene, uint64_t nowMs);
}  // namespace PBC

#endif
