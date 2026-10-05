// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
#include "pbc_service.h"
#include "pbc_json.h"
#include <algorithm>
#include <charconv>

namespace PBC
{
namespace
{
std::string StringObjectSchema(std::initializer_list<std::string> fields)
{
    pbc_json schema = {{"type", "object"}, {"additionalProperties", false},
        {"properties", pbc_json::object()}, {"required", pbc_json::array()}};
    for (auto const& field : fields)
    {
        schema["properties"][field] = {{"type", "string"}};
        schema["required"].push_back(field);
    }
    return schema.dump();
}

bool Protected(NoteRecord const& note)
{
    return note.authority != "model" || note.kind == "fact" || (note.kind == "commitment" && !note.resolved);
}
}

CharacterService::CharacterService(CharacterStore& store, ModelGateway& model, ApiLedger& ledger,
    CharacterPrompts prompts) : _store(store), _model(model), _ledger(ledger), _prompts(std::move(prompts)) { }

bool CharacterService::EstablishFoundation(ActorRecord& actor, CharacterInput const& input)
{
    if (!actor.foundation.empty() || actor.kind == "watch" || actor.kind == "generic_npc")
        return true;
    auto facts = pbc_json::parse(input.gameFactsJson, nullptr, false);
    if (!facts.is_object())
        return false;
    if (actor.kind == "named_npc")
    {
        // Prepared NPC identity never spends a biography call or invents missing canon.
        std::string foundation;
        for (auto const& record : facts.value("knowledge", pbc_json::array()))
            if (record.value("layer", "") == "npc")
                foundation += record.value("text", "") + "\n";
        if (facts.contains("canon") && facts["canon"].is_string())
            foundation += facts["canon"].get<std::string>();
        if (foundation.empty())
            foundation = actor.name + ": use only supplied native role and current facts; personal background is unknown.";
        if (!_store.Foundation(actor.id, actor.version, foundation, actor.name + ": established NPC."))
            return false;
        auto current = _store.Actor(actor.id);
        if (!current)
            return false;
        actor = std::move(*current);
        return true;
    }
    facts["owner_facts"] = pbc_json::array();
    for (auto const& note : _store.Notes(actor.id, 5000, true))
        if (note.authority == "owner" && note.kind == "fact")
            facts["owner_facts"].push_back(note.text);
    if (!_store.Healthy())
        return false;
    // Chat claims are intentionally absent from the foundation request. The caller
    // supplies actual race/class/level, place, realm phase and known canon/legacy card.
    pbc_json context = {{"task", "biography"}, {"id", actor.id}, {"name", actor.name},
        {"kind", actor.kind}, {"game_facts", facts}};
    auto request = input.request;
    request.reason = "biography";
    request.actorId = actor.id;
    auto reply = _model.Generate(_prompts.foundation, context.dump(),
        StringObjectSchema({"foundation", "summary"}), request);
    if (!reply.success)
        return false;
    auto parsed = pbc_json::parse(reply.text, nullptr, false);
    bool valid = parsed.is_object() && parsed.size() == 2 &&
        parsed.contains("foundation") && parsed["foundation"].is_string() &&
        parsed.contains("summary") && parsed["summary"].is_string();
    bool saved = valid && _store.Foundation(actor.id, actor.version,
        parsed["foundation"].get<std::string>(), parsed["summary"].get<std::string>());
    _ledger.RecordDelivery(reply.requestId, saved ? "stored" : "discarded");
    auto current = _store.Actor(actor.id);
    if (!current || current->foundation.empty())
        return false;
    actor = std::move(*current);
    return true;
}

CharacterTurn CharacterService::Generate(CharacterInput const& input, bool silent)
{
    CharacterTurn turn;
    if (!silent && !Compact(input.actorId, input.request))
    {
        turn.error = "memory_compaction_unavailable";
        return turn;
    }
    auto actor = _store.Actor(input.actorId);
    if (!actor)
    {
        turn.error = "actor_unavailable";
        return turn;
    }
    if (!silent && !EstablishFoundation(*actor, input))
    {
        turn.error = "foundation_unavailable";
        return turn;
    }
    turn.actor = *actor;
    turn.watchId = input.watchId;
    turn.informationGroups = input.informationGroups;
    if (!input.watchId.empty() && std::none_of(turn.informationGroups.begin(), turn.informationGroups.end(),
        [&](auto const& group) { return group.id == input.watchId; }))
        turn.informationGroups.push_back({input.watchId, "Local watch", "", 259200000, true});
    ContextInput context;
    context.actor = *actor;
    context.task = silent ? "memory" : "dialogue";
    context.instructions = silent ? _prompts.memory : _prompts.dialogue;
    context.gameFactsJson = input.gameFactsJson;
    // Deferred extraction has no current utterance. A caller's stale dialogue
    // text must not compete with the pending witnessed sources.
    context.currentContribution = silent ? "" : input.contribution;
    context.observations = _store.Observations(actor->id, silent,
        actor->kind == "generic_npc" ? 32 : 512);
    if (actor->kind == "generic_npc" && input.nowMs)
        std::erase_if(context.observations, [&](auto const& observation)
        {
            return input.nowMs > observation.createdMs && input.nowMs - observation.createdMs > 300000;
        });
    if (actor->kind != "generic_npc")
        context.personalNotes = _store.Notes(actor->id, 5000, true);
    for (auto const& group : turn.informationGroups)
    {
        context.readableGroups.insert(group.id);
        auto facts = pbc_json::parse(input.gameFactsJson, nullptr, false);
        if (group.publicReports || (facts.is_object() && facts.value("channel", "") == "guild"))
            context.reportGroups.insert(group.id);
        auto reports = _store.Reports(group.id, input.subjects, input.nowMs, group.reportLifetimeMs);
        context.groupReports.insert(context.groupReports.end(), reports.begin(), reports.end());
    }
    context.subjects = input.subjects;
    context.subjects.insert(actor->id);
    for (auto const& observation : context.observations)
        context.subjects.insert(observation.authorId);
    context.animations = input.animations;
    if (!silent)
        context.actionOptions = input.actionOptions;
    context.mayReportToWatch = !input.watchId.empty() || actor->kind == "watch";
    context.remainingTurns = input.remainingTurns;
    context.contextTokens = _model.Settings().contextTokens;
    context.outputTokens = _model.Settings().outputTokens;
    if (!_store.Healthy())
    {
        turn.error = "storage_unavailable";
        return turn;
    }
    if (silent && context.observations.empty())
    {
        turn.success = true;
        return turn;
    }
    auto built = BuildCharacterContext(context);
    if (!built.success)
    {
        turn.error = built.error;
        return turn;
    }
    auto request = input.request;
    request.actorId = actor->id;
    if (silent)
    {
        request.reason = "memory";
        request.background = true;
    }
    built.permissions.maxSegments = input.maxSegments;
    auto response = _model.Generate(built.system, built.user,
        DialogueSchema(built.permissions.animations, built.permissions.actionOptions, input.maxSegments), request);
    turn.requestId = response.requestId;
    if (!response.success)
    {
        turn.error = response.error;
        return turn;
    }
    auto parsed = ParseDialogue(response.text, built.permissions);
    if (!parsed || (silent && !parsed->segments.empty()))
    {
        _ledger.RecordDelivery(response.requestId, "malformed");
        turn.error = "invalid_character_output";
        // Diagnose contract failures without logging private dialogue or provider content.
        auto output = pbc_json::parse(response.text, nullptr, false);
        if (output.is_object() && output.contains("segments") && output["segments"].is_array())
        {
            std::size_t longest = 0;
            for (auto const& segment : output["segments"])
                if (segment.is_object() && segment.contains("text") && segment["text"].is_string())
                    longest = std::max(longest, segment["text"].get_ref<std::string const&>().size());
            turn.error += ":segments=" + std::to_string(output["segments"].size()) +
                ":max_text_bytes=" + std::to_string(longest);
        }
        return turn;
    }
    turn.dialogue = std::move(*parsed);
    // Invalid proposals must not retire the pending evidence. A valid, deliberately
    // empty note list does retire this extraction pass; raw observations stay archived.
    if (!turn.dialogue.rejectedNotes)
        turn.covered = std::move(built.includedSources);
    turn.success = true;
    return turn;
}

CharacterTurn CharacterService::Reply(CharacterInput const& input)
{
    return Generate(input, false);
}

bool CharacterService::Remember(CharacterInput const& input, uint64_t nowMs)
{
    auto turn = Generate(input, true);
    return turn.success && Persist(turn, {}, "silent", nowMs);
}

bool CharacterService::Persist(CharacterTurn const& turn,
    std::map<std::size_t, SourceVersion> const& delivered, std::string const& outcome, uint64_t nowMs)
{
    if (!turn.success)
        return false;
    std::vector<NoteRecord> notes;
    for (std::size_t index = 0; index < turn.dialogue.notes.size(); ++index)
    {
        auto const& proposal = turn.dialogue.notes[index];
        if (outcome == "cancelled" && proposal.scope != "personal")
            continue; // Cancelled membership/context work cannot publish an undelivered report.

        if (turn.actor.kind == "generic_npc" && proposal.scope == "personal")
            continue; // Generic spawns have immediate context and institutional reports only.
        NoteRecord note;
        note.owner = proposal.scope == "watch" ? turn.watchId :
                     proposal.scope.starts_with("group:") ? proposal.scope.substr(6) : turn.actor.id;
        if (turn.actor.kind == "watch")
            note.owner = turn.actor.id;
        if (note.owner.empty())
            continue;
        note.operationId = turn.requestId + ":note:" + std::to_string(index);
        note.subject = proposal.subject;
        note.kind = proposal.kind;
        note.text = proposal.text;
        note.createdMs = nowMs;
        bool supported = true;
        for (auto const& reference : proposal.sources)
        {
            if (reference.starts_with("reply:"))
            {
                std::size_t segment = 0;
                auto parsed = std::from_chars(reference.data() + 6, reference.data() + reference.size(), segment);
                auto found = delivered.find(segment);
                if (parsed.ec != std::errc() || parsed.ptr != reference.data() + reference.size() ||
                    found == delivered.end())
                {
                    supported = false;
                    break;
                }
                note.sources.push_back(found->second);
            }
            else if (auto source = SourceVersion::Parse(reference))
                note.sources.push_back(*source);
            else
                supported = false;
        }
        if (supported)
            notes.push_back(std::move(note));
    }
    std::set<std::string> groups;
    for (auto const& group : turn.informationGroups)
        groups.insert(group.id);
    bool saved = _store.ExtractGroups(turn.actor.id, turn.actor.version, groups, notes, turn.covered);
    if (!turn.requestId.empty())
        _ledger.RecordDelivery(turn.requestId, outcome);
    return saved;
}

bool CharacterService::Compact(std::string const& actorId, ApiReservation request,
    uint32_t triggerTokens, uint32_t targetTokens)
{
    auto actor = _store.Actor(actorId);
    if (!actor)
        return false;
    // Institutional reports retain subject-specific records for relevant lookup;
    // a mixed global narrative would lose that boundary and cost an unused call.
    if (actor->kind == "watch" || actor->kind == "generic_npc")
        return true;
    auto all = _store.Notes(actorId, 5000, true);
    if (!_store.Healthy())
        return false;
    std::sort(all.begin(), all.end(), [](auto const& left, auto const& right) { return left.id < right.id; });
    std::vector<NoteRecord> inputs;
    pbc_json entries = pbc_json::array();
    pbc_json protectedNotes = pbc_json::array();
    std::size_t bytes = actor->recall.size();
    for (auto const& note : all)
    {
        if (Protected(note))
        {
            protectedNotes.push_back({{"kind", note.kind}, {"text", note.text}, {"authority", note.authority}});
            continue;
        }
        if (note.compacted)
            continue;
        // Compact a stable prefix with prompt headroom. Remaining arrivals/notes
        // stay appended for a later pass instead of overflowing the model context.
        if (!inputs.empty() && bytes + note.text.size() > static_cast<uint64_t>(triggerTokens) * 5)
            continue;
        bytes += note.text.size();
        entries.push_back({{"id", note.id}, {"subject", note.subject}, {"kind", note.kind},
            {"text", note.text}, {"created_ms", note.createdMs}});
        inputs.push_back(note);
    }
    if (inputs.empty() || bytes < static_cast<uint64_t>(triggerTokens) * 4)
        return true;
    uint32_t outputTokens = std::clamp(targetTokens + 1024, 2048u, 8192u);
    pbc_json context = {{"task", "compaction"}, {"self", actor->name}, {"foundation", actor->foundation},
        {"recall", actor->recall}, {"appended_notes", entries}, {"protected_notes", protectedNotes},
        {"target_tokens", std::min(targetTokens, outputTokens - 1024)}};
    request.actorId = actorId;
    request.reason = "compaction";
    request.background = true;
    auto response = _model.Generate(_prompts.recall, context.dump(), StringObjectSchema({"recall"}), request, outputTokens);
    if (!response.success)
        return false;
    auto parsed = pbc_json::parse(response.text, nullptr, false);
    bool valid = parsed.is_object() && parsed.size() == 1 && parsed.contains("recall") &&
        parsed["recall"].is_string();
    bool saved = valid && _store.Compact(actorId, actor->version, actor->recallVersion,
        parsed["recall"].get<std::string>(), inputs);
    _ledger.RecordDelivery(response.requestId, saved ? "stored" : "discarded");
    return saved;
}
}
