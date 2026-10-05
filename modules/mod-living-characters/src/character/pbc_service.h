// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Living Azeroth: one character pipeline for dialogue, silent observation and recall.
#ifndef PBC_SERVICE_H
#define PBC_SERVICE_H

#include "pbc_context.h"
#include "pbc_knowledge.h"
#include "pbc_model.h"
#include <map>

namespace PBC
{
struct CharacterPrompts
{
    std::string dialogue;
    std::string foundation;
    std::string memory;
    std::string recall;
};

struct CharacterInput
{
    std::string actorId;
    std::string watchId; // Legacy explicit watch mapping.
    std::vector<InformationGroup> informationGroups;
    std::string gameFactsJson = "{}";
    std::string contribution;
    std::set<std::string> subjects;
    std::set<std::string> animations;
    std::map<std::string, std::string> actionOptions;
    uint8_t remainingTurns = 1;
    uint8_t maxSegments = 6;
    uint64_t nowMs = 0;
    ApiReservation request;
};

struct CharacterTurn
{
    bool success = false;
    std::string error;
    ActorRecord actor;
    std::string watchId; // Legacy explicit watch mapping.
    std::vector<InformationGroup> informationGroups;
    std::string requestId;
    Dialogue dialogue;
    std::vector<SourceVersion> covered;
};

// Worker-only API. The world coordinator supplies immutable game facts and validates
// live audience/delivery. Networking never holds a storage transaction or game pointer.
class CharacterService
{
public:
    CharacterService(CharacterStore& store, ModelGateway& model, ApiLedger& ledger, CharacterPrompts prompts);
    CharacterTurn Reply(CharacterInput const& input);
    bool Remember(CharacterInput const& input, uint64_t nowMs);
    bool Persist(CharacterTurn const& turn, std::map<std::size_t, SourceVersion> const& delivered,
        std::string const& outcome, uint64_t nowMs);
    bool Compact(std::string const& actorId, ApiReservation request,
        uint32_t triggerTokens = 6000, uint32_t targetTokens = 3000);

private:
    CharacterTurn Generate(CharacterInput const& input, bool silent);
    bool EstablishFoundation(ActorRecord& actor, CharacterInput const& input);
    CharacterStore& _store;
    ModelGateway& _model;
    ApiLedger& _ledger;
    CharacterPrompts _prompts;
};
}

#endif
