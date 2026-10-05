#include "pbc_service.h"
#include "pbc_conversation.h"
#include "pbc_json.h"
#include "MySQLThreading.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdlib>
#include <functional>

namespace
{
class RecordedLedger final : public PBC::ApiLedger
{
public:
    std::vector<PBC::ApiReservation> calls;
    bool Reserve(PBC::ApiReservation const& request) override { calls.push_back(request); return true; }
    bool MarkDispatched(std::string const&) override { return true; }
    bool CancelUnsent(std::string const&) override { return true; }
    bool Reconcile(std::string const&, PBC::ApiUsage const&) override { return true; }
    bool RecordDelivery(std::string const&, std::string const&) override { return true; }
    std::optional<PBC::BudgetTotals> Totals() override { return PBC::BudgetTotals{}; }
};

PBC_HttpResponse Recorded(pbc_json const& content)
{
    return {200, pbc_json{{"id", "recorded-response"}, {"usage", {{"cost", 0}}},
        {"choices", {{{"finish_reason", "stop"}, {"message", {{"content", content.dump()}}}}}}}.dump()};
}
}

TEST(PBCService, FoundationAndInterruptedPromiseUseRealStoreAndScheduler)
{
    char const* info = std::getenv("PBC_BUDGET_TEST_INFO");
    char const* run = std::getenv("PBC_BUDGET_TEST_ID");
    if (!info || !run)
        GTEST_SKIP() << "Requires isolated character database";
    MySQL::Library_Init();
    struct Guard { ~Guard() { MySQL::Library_End(); } } guard;
    PBC::CharacterStore store;
    ASSERT_TRUE(store.Open(info));
    std::string actor = std::string(run) + ":service";
    ASSERT_TRUE(store.EnsureActor({actor, "bot", 1, "Anacea"}));
    PBC::ObservationRecord event;
    event.eventKey = std::string(run) + ":service-in";
    event.sceneId = run;
    event.authorId = "player:1";
    event.channel = "say";
    event.text = "Your brother died at Icecrown; come with me.";
    event.createdMs = 100;
    auto source = store.Observe(event, {actor});
    ASSERT_TRUE(source);
    RecordedLedger ledger;
    PBC::ModelSettings settings;
    settings.apiKey = "recorded-test-key";
    unsigned calls = 0;
    PBC::ModelGateway model(settings, ledger, [&](auto const&, auto const& body, auto const&, uint32_t)
    {
        ++calls;
        auto request = pbc_json::parse(body);
        auto context = pbc_json::parse(request["messages"][1]["content"].template get<std::string>());
        if (calls == 1)
        {
            EXPECT_EQ(context["game_facts"]["level"], 1);
            EXPECT_FALSE(context.contains("current_contribution"));
            return Recorded({{"foundation", "A young priest with no brother, curious about local travellers."},
                {"summary", "A curious novice with a dry sense of humour."}});
        }
        EXPECT_EQ(context["self"]["foundation"],
            "A young priest with no brother, curious about local travellers.");
        return Recorded({{"segments", {{{"kind", "speech"}, {"text", "I have no brother. Who did you meet?"},
                {"animation", ""}}, {{"kind", "emote"}, {"text", "looks toward the road"}, {"animation", ""}},
                {{"kind", "speech"}, {"text", "I will meet you at the inn."}, {"animation", ""}}}},
            {"notes", {{{"kind", "memory"}, {"text", "Orion claimed I had a brother; I am unsure who he meant."},
                {"subject", "player:1"}, {"scope", "personal"}, {"sources", {source->Key()}}},
                {{"kind", "commitment"}, {"text", "I promised to meet Orion at the inn."},
                {"subject", "player:1"}, {"scope", "personal"}, {"sources", {"reply:2"}}}}}});
    });
    PBC::CharacterService service(store, model, ledger, {"dialogue", "foundation", "memory", "recall"});
    PBC::CharacterInput input;
    input.actorId = actor;
    input.gameFactsJson = R"({"level":1,"race":"human","class":"priest","phase":"before Northrend"})";
    input.contribution = event.text;
    input.subjects = {actor, "player:1"};
    input.request.reason = "direct";
    auto turn = service.Reply(input);
    ASSERT_TRUE(turn.success) << turn.error;
    ASSERT_EQ(turn.dialogue.notes.size(), 2u);
    PBC::Conversation scene;
    ASSERT_TRUE(scene.Begin(PBC::Contribution::Human, true, {actor}, 2, 0));
    ASSERT_TRUE(scene.Choose(scene.Version(), actor, turn.actor.version));
    ASSERT_TRUE(scene.Accept(scene.Version(), turn.actor.version, turn.dialogue, turn.requestId, 0));
    auto segment = scene.Due(0);
    ASSERT_TRUE(segment);
    auto spoken = event;
    spoken.eventKey = std::string(run) + ":service-out";
    spoken.authorId = actor;
    spoken.evidence = "delivered";
    spoken.text = segment->segment.text;
    auto heard = store.Observe(spoken, {actor});
    ASSERT_TRUE(heard);
    ASSERT_TRUE(scene.Delivered(scene.Version(), 0, 0));
    scene.Abort();
    ASSERT_TRUE(service.Persist(turn, {{0, *heard}}, scene.DeliveryOutcome(), 1000));
    auto notes = store.Notes(actor);
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0].kind, "memory");
    EXPECT_EQ(calls, 2u);
    EXPECT_EQ(ledger.calls[0].reason, "biography");
    EXPECT_EQ(ledger.calls[1].reason, "direct");
    auto next = service.Reply(input);
    ASSERT_TRUE(next.success);
    EXPECT_EQ(calls, 3u); // No repeat biography on the next conversation.
}

TEST(PBCService, SilentExtractionKeepsInvalidSourcesAndRejectsStaleCompaction)
{
    char const* info = std::getenv("PBC_BUDGET_TEST_INFO");
    char const* run = std::getenv("PBC_BUDGET_TEST_ID");
    if (!info || !run)
        GTEST_SKIP() << "Requires isolated character database";
    MySQL::Library_Init();
    struct Guard { ~Guard() { MySQL::Library_End(); } } guard;
    PBC::CharacterStore store;
    ASSERT_TRUE(store.Open(info));
    std::string actor = std::string(run) + ":silent-service";
    ASSERT_TRUE(store.EnsureActor({actor, "bot", 1, "Anacea"}));
    PBC::ObservationRecord event;
    event.eventKey = std::string(run) + ":silent-in";
    event.sceneId = run;
    event.authorId = "player:1";
    event.channel = "say";
    event.text = "I used to owe the innkeeper money, but paid yesterday.";
    event.createdMs = 100;
    auto source = store.Observe(event, {actor});
    ASSERT_TRUE(source);
    RecordedLedger ledger;
    PBC::ModelSettings settings;
    settings.apiKey = "recorded-test-key";
    unsigned calls = 0;
    PBC::ModelGateway model(settings, ledger, [&](auto const&, auto const&, auto const&, uint32_t)
    {
        ++calls;
        if (calls == 3)
        {
            auto notes = store.Notes(actor);
            EXPECT_EQ(notes.size(), 1u);
            EXPECT_TRUE(store.EditNote(actor, notes[0].id, notes[0].version,
                "Orion clarified that the debt was to the tailor, and is paid.", 1, false));
            return Recorded({{"recall", "Orion owed the innkeeper and says he paid."}});
        }
        return Recorded({{"segments", pbc_json::array()}, {"notes", {{{"kind", "memory"},
            {"text", "Orion says his debt to the innkeeper was paid yesterday."},
            {"subject", "player:1"}, {"scope", "personal"},
            {"sources", {calls == 1 ? "999999999:1" : source->Key()}}}}}});
    });
    PBC::CharacterService service(store, model, ledger, {"dialogue", "foundation", "memory", "recall"});
    PBC::CharacterInput input;
    input.actorId = actor;
    input.request.reason = "memory";
    ASSERT_TRUE(service.Remember(input, 300100));
    EXPECT_EQ(store.Observations(actor, true).size(), 1u); // Invalid proposal did not retire evidence.
    EXPECT_TRUE(store.Notes(actor).empty());
    ASSERT_TRUE(service.Remember(input, 300200));
    EXPECT_TRUE(store.Observations(actor, true).empty());
    EXPECT_FALSE(service.Compact(actor, input.request, 1, 1)); // Owner edited during the model call.
    EXPECT_TRUE(store.Actor(actor)->recall.empty());
    EXPECT_EQ(store.Notes(actor)[0].authority, "owner");
    EXPECT_EQ(ledger.calls.back().reason, "compaction");
    EXPECT_EQ(calls, 3u); // Silent memory does not force a biography or spoken reply.
}

TEST(PBCService, LargeBacklogCompactsAPrefixAndRetainsProtectedAndConcurrentNotes)
{
    char const* info = std::getenv("PBC_BUDGET_TEST_INFO");
    char const* run = std::getenv("PBC_BUDGET_TEST_ID");
    if (!info || !run)
        GTEST_SKIP() << "Requires isolated character database";
    MySQL::Library_Init();
    struct Guard { ~Guard() { MySQL::Library_End(); } } guard;
    PBC::CharacterStore store;
    ASSERT_TRUE(store.Open(info));
    std::string actor = std::string(run) + ":backlog";
    ASSERT_TRUE(store.EnsureActor({actor, "bot", 1, "Anacea"}));
    PBC::ObservationRecord event;
    event.eventKey = actor + ":source";
    event.sceneId = run;
    event.authorId = "player:1";
    event.channel = "say";
    event.text = "Orion described his travels and promised to return.";
    auto source = store.Observe(event, {actor});
    ASSERT_TRUE(source);
    std::vector<PBC::NoteRecord> notes;
    for (unsigned i = 0; i < 100; ++i)
    {
        PBC::NoteRecord note;
        note.operationId = actor + ":note:" + std::to_string(i);
        note.owner = actor;
        note.subject = "player:1";
        note.kind = i == 99 ? "commitment" : "memory";
        note.text = i == 99 ? "Orion promised to return." : std::string(800, 'a');
        note.sources = {*source};
        notes.push_back(note);
    }
    ASSERT_TRUE(store.Extract(actor, 1, "", notes, {*source}));
    RecordedLedger ledger;
    PBC::ModelSettings settings;
    settings.apiKey = "recorded-test-key";
    std::size_t compacted = 0;
    PBC::ModelGateway model(settings, ledger, [&](auto const&, auto const& body, auto const&, uint32_t)
    {
        auto request = pbc_json::parse(body);
        auto context = pbc_json::parse(request["messages"][1]["content"].template get<std::string>());
        EXPECT_EQ(request["max_tokens"], 4024);
        EXPECT_EQ(context["target_tokens"], 3000);
        EXPECT_EQ(context["protected_notes"].size(), 1u);
        compacted = context["appended_notes"].size();
        EXPECT_GT(compacted, 20u);
        EXPECT_LT(compacted, 99u);
        auto arrival = notes[0];
        arrival.operationId = actor + ":concurrent";
        arrival.text = "A newer unprocessed recollection arrived during compaction.";
        EXPECT_TRUE(store.Extract(actor, 1, "", {arrival}, {}));
        return Recorded({{"recall", "Orion described travelling; details remain in source records."}});
    });
    PBC::CharacterService service(store, model, ledger, {"dialogue", "foundation", "memory", "recall"});
    ASSERT_TRUE(service.Compact(actor, {}));
    EXPECT_EQ(store.Notes(actor).size(), 101u);
    auto active = store.Notes(actor, 1024, true);
    EXPECT_EQ(active.size(), 101u - compacted);
    EXPECT_TRUE(std::any_of(active.begin(), active.end(), [](auto const& note)
        { return note.kind == "commitment" && !note.compacted; }));
    EXPECT_TRUE(std::any_of(active.begin(), active.end(), [](auto const& note)
        { return note.text.starts_with("A newer") && !note.compacted; }));
    EXPECT_EQ(store.Actor(actor)->recallVersion, 1u);
}

TEST(PBCService, PreparedNpcIdentityNeedsNoBiographyCallAndQuestFactsRefresh)
{
    char const* info = std::getenv("PBC_BUDGET_TEST_INFO");
    char const* run = std::getenv("PBC_BUDGET_TEST_ID");
    if (!info || !run)
        GTEST_SKIP() << "Requires isolated character database";
    MySQL::Library_Init();
    struct Guard { ~Guard() { MySQL::Library_End(); } } guard;
    PBC::CharacterStore store;
    ASSERT_TRUE(store.Open(info));
    std::string actor = std::string(run) + ":prepared-npc";
    ASSERT_TRUE(store.EnsureActor({actor, "named_npc", 197, "Marshal McBride"}));
    RecordedLedger ledger;
    PBC::ModelSettings settings;
    settings.apiKey = "recorded-test-key";
    unsigned calls = 0;
    PBC::ModelGateway model(settings, ledger, [&](auto const&, auto const& body, auto const&, uint32_t)
    {
        ++calls;
        auto request = pbc_json::parse(body);
        auto context = pbc_json::parse(request["messages"][1]["content"].template get<std::string>());
        EXPECT_EQ(context.value("task", "dialogue"), "dialogue");
        EXPECT_NE(context["self"]["foundation"].template get<std::string>().find("Marshal"), std::string::npos);
        EXPECT_EQ(context["game_facts"]["interaction_quests"]["7"], calls == 1 ? "incomplete" : "rewarded");
        return Recorded({{"segments", {{{"kind", "speech"}, {"text", "Good day, traveller."},
            {"animation", ""}}}}, {"notes", pbc_json::array()}});
    });
    PBC::CharacterService service(store, model, ledger, {"dialogue", "foundation", "memory", "recall"});
    PBC::CharacterInput input;
    input.actorId = actor;
    input.request.reason = "direct";
    auto facts = pbc_json::parse(R"({"knowledge":[{"layer":"npc","text":"Marshal McBride oversees work at the abbey."}],"interaction_quests":{"7":"incomplete"}})");
    input.gameFactsJson = facts.dump();
    ASSERT_TRUE(service.Reply(input).success);
    facts["interaction_quests"]["7"] = "rewarded";
    input.gameFactsJson = facts.dump();
    ASSERT_TRUE(service.Reply(input).success);
    EXPECT_EQ(calls, 2u);
    ASSERT_EQ(ledger.calls.size(), 2u);
    for (auto const& call : ledger.calls)
        EXPECT_EQ(call.reason, "direct");
    EXPECT_EQ(store.Actor(actor)->foundation.find("incomplete"), std::string::npos);
}
