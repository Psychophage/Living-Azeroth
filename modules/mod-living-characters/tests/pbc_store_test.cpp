#include "pbc_store.h"
#include "MySQLThreading.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdlib>

TEST(PBCStore, GroupsDeduplicateEvidenceExpireReportsAndRejectPrivateOrForeignSources)
{
    char const* info = std::getenv("PBC_BUDGET_TEST_INFO");
    char const* run = std::getenv("PBC_BUDGET_TEST_ID");
    if (!info || !run)
        GTEST_SKIP() << "Requires isolated character database";
    MySQL::Library_Init();
    struct Guard { ~Guard() { MySQL::Library_End(); } } guard;
    PBC::CharacterStore store;
    ASSERT_TRUE(store.Open(info));
    std::string speaker = std::string(run) + ":group-speaker";
    std::string guild = std::string(run) + ":guild";
    std::string other = std::string(run) + ":foreign-guild";
    ASSERT_TRUE(store.EnsureActor({speaker, "bot", 1, "Courier"}));
    ASSERT_TRUE(store.EnsureActor({guild, "watch", 0, "First guild"}));
    ASSERT_TRUE(store.EnsureActor({other, "watch", 0, "Other guild"}));
    PBC::ObservationRecord event;
    event.eventKey = std::string(run) + ":guild-event";
    event.sceneId = run;
    event.authorId = "player:1";
    event.channel = "guild";
    event.text = "A member reports a missing parcel.";
    event.createdMs = 100;
    auto source = store.Observe(event, {speaker, guild});
    ASSERT_TRUE(source);
    PBC::NoteRecord note;
    note.operationId = std::string(run) + ":guild-report";
    note.owner = guild;
    note.kind = "report";
    note.text = "A member said a parcel was missing; unverified.";
    note.createdMs = 1000;
    note.sources = {*source};
    EXPECT_FALSE(store.ExtractGroups(speaker, 1, {}, {note}, {*source}));
    ASSERT_TRUE(store.ExtractGroups(speaker, 1, {guild}, {note}, {*source}));
    note.operationId += ":repeat";
    ASSERT_TRUE(store.ExtractGroups(speaker, 1, {guild}, {note}, {*source}));
    ASSERT_EQ(store.Reports(guild, {}, 200, 500).size(), 1u);
    EXPECT_EQ(store.Reports(guild, {}, 200, 500)[0].createdMs, 100u);
    EXPECT_TRUE(store.Reports(guild, {}, 601, 500).empty()); // Age belongs to evidence, not extraction.
    EXPECT_TRUE(store.Reports(other, {}, 200, 500).empty());
    note.owner = other;
    EXPECT_FALSE(store.ExtractGroups(speaker, 1, {other}, {note}, {*source}));
    event.channel = "whisper";
    event.eventKey += ":private";
    auto secret = store.Observe(event, {speaker, guild});
    ASSERT_TRUE(secret);
    note.owner = guild;
    note.sources = {*secret};
    EXPECT_FALSE(store.ExtractGroups(speaker, 1, {guild}, {note}, {*secret}));
    ASSERT_TRUE(store.ReviseSource(*source, "The parcel was found.", false, true));
    EXPECT_TRUE(store.Reports(guild, {}, 200, 500).empty());
    EXPECT_TRUE(store.Source(speaker, source->id)); // Correction preserves source history.
}

TEST(PBCStore, SilentWitnessesCorrectionsAndConcurrentCompaction)
{
    char const* info = std::getenv("PBC_BUDGET_TEST_INFO");
    char const* run = std::getenv("PBC_BUDGET_TEST_ID");
    if (!info || !run)
        GTEST_SKIP() << "Requires the explicitly provisioned isolated character-store fixture";
    MySQL::Library_Init();
    struct LibraryGuard
    {
        ~LibraryGuard() { MySQL::Library_End(); }
    } libraryGuard;
    PBC::CharacterStore store;
    ASSERT_TRUE(store.Open(info));
    std::string anacea = std::string(run) + ":anacea";
    std::string npc = std::string(run) + ":guard";
    std::string watch = std::string(run) + ":watch";
    std::string elsewhere = std::string(run) + ":other-watch";
    ASSERT_TRUE(store.EnsureActor({anacea, "bot", 1, "Anacea"}));
    ASSERT_TRUE(store.EnsureActor({npc, "named_npc", 0, "Bram"}));
    ASSERT_TRUE(store.EnsureActor({watch, "watch", 0, "Local watch"}));
    ASSERT_TRUE(store.EnsureActor({elsewhere, "watch", 0, "Other town"}));
    ASSERT_TRUE(store.Foundation(anacea, 1, "An apprentice priest with no siblings.", "Curious, cautious priest."));
    EXPECT_FALSE(store.Foundation(anacea, 2, "A champion who fought the Lich King.", "Hero"));
    std::string human = std::string(run) + ":human";
    std::string passerby = std::string(run) + ":passerby";
    ASSERT_TRUE(store.EnsureActor({human, "player", 1, "Orion"}));
    ASSERT_TRUE(store.EnsureActor({passerby, "bot", 2, "Velanthel"}));

    // Witnessing a player's quest or fight never earns a background model call on its own.
    PBC::ObservationRecord questEvent;
    questEvent.eventKey = std::string(run) + ":quest";
    questEvent.sceneId = run;
    questEvent.authorId = human;
    questEvent.channel = "event";
    questEvent.createdMs = 100;
    questEvent.text = "Orion completed the quest: Solanian's Belongings";
    ASSERT_TRUE(store.Observe(questEvent, {passerby}));
    auto quiet = store.PendingActors(300100, 300000, 4096, 64);
    EXPECT_EQ(std::find(quiet.begin(), quiet.end(), passerby), quiet.end());

    PBC::ObservationRecord publicLine;
    publicLine.eventKey = std::string(run) + ":public";
    publicLine.sceneId = run;
    publicLine.authorId = human;
    publicLine.channel = "say";
    publicLine.createdMs = 100;
    publicLine.text = "Orion says he has a debt to settle.";
    auto source = store.Observe(publicLine, {anacea, npc, watch});
    ASSERT_TRUE(source);
    ASSERT_TRUE(store.Observe(publicLine, {anacea, npc})); // Idempotent incoming hook/retry.
    auto privateLine = publicLine;
    privateLine.eventKey = std::string(run) + ":private";
    privateLine.channel = "whisper";
    privateLine.text = "A private disclosure Anacea cannot hear.";
    auto secret = store.Observe(privateLine, {npc});
    ASSERT_TRUE(secret);

    PBC::CharacterStore afterRestart;
    ASSERT_TRUE(afterRestart.Open(info));
    auto actor = afterRestart.Actor(anacea);
    ASSERT_TRUE(actor);
    EXPECT_EQ(actor->ownerGuid, 1u);
    EXPECT_EQ(actor->foundation, "An apprentice priest with no siblings.");
    EXPECT_EQ(afterRestart.Observations(anacea, true).size(), 1u);
    EXPECT_EQ(afterRestart.Observations(npc, true).size(), 2u);
    EXPECT_TRUE(afterRestart.Source(anacea, source->id));
    EXPECT_FALSE(afterRestart.Source(anacea, secret->id));
    auto early = store.PendingActors(300099);
    EXPECT_EQ(std::find(early.begin(), early.end(), anacea), early.end());
    auto due = store.PendingActors(300100, 300000, 4096, 64);
    EXPECT_NE(std::find(due.begin(), due.end(), anacea), due.end());

    PBC::NoteRecord note;
    note.operationId = std::string(run) + ":memory";
    note.owner = anacea;
    note.subject = "player:1";
    note.kind = "memory";
    note.text = "Orion told me he owes a debt; I do not know whether his account is accurate.";
    note.createdMs = 300100;
    note.sources = {*secret};
    EXPECT_FALSE(store.Extract(anacea, actor->version, "", {note}, {*secret}));
    EXPECT_EQ(store.Observations(anacea, true).size(), 1u);
    note.sources = {*source};
    ASSERT_TRUE(store.Extract(anacea, actor->version, "", {note}, {*source}));
    EXPECT_TRUE(store.Observations(anacea, true).empty());
    due = store.PendingActors(300100, 300000, 4096, 64);
    EXPECT_EQ(std::find(due.begin(), due.end(), anacea), due.end());
    EXPECT_EQ(store.Observations(anacea, false).size(), 1u); // Extraction never deletes raw evidence.

    auto report = note;
    report.owner = watch;
    report.kind = "report";
    report.text = "Bram reported hearing Orion mention a debt; other guards were not present.";
    report.sources = {*secret};
    EXPECT_FALSE(store.Extract(npc, 1, watch, {report}, {*secret}));
    report.sources = {*source};
    ASSERT_TRUE(store.Extract(npc, 1, watch, {report}, {*source}));
    EXPECT_EQ(store.Notes(watch).size(), 1u);
    EXPECT_EQ(store.Reports(watch, {"player:1"}).size(), 1u);
    EXPECT_TRUE(store.Reports(watch, {"player:2"}).empty());
    EXPECT_TRUE(store.Reports(elsewhere, {"player:1"}).empty());
    EXPECT_TRUE(store.Observations(watch, true).empty()); // A spoken guard's report covers the shared extraction.
    EXPECT_TRUE(store.Notes(elsewhere).empty());

    auto compactInputs = store.Notes(anacea);
    ASSERT_EQ(compactInputs.size(), 1u);
    auto later = publicLine;
    later.eventKey = std::string(run) + ":later";
    later.createdMs = 300200;
    later.text = "Orion says he is looking for honest work.";
    auto laterSource = store.Observe(later, {anacea});
    ASSERT_TRUE(laterSource);
    auto laterNote = note;
    laterNote.operationId = std::string(run) + ":later-memory";
    laterNote.text = "Orion wants to find honest work.";
    laterNote.sources = {*laterSource};
    ASSERT_TRUE(store.Extract(anacea, actor->version, "", {laterNote}, {*laterSource}));
    EXPECT_FALSE(store.Compact(anacea, actor->version, 0, "", compactInputs));
    ASSERT_TRUE(store.Compact(anacea, actor->version, 0, "Orion claims to owe a debt.", compactInputs));
    auto notes = store.Notes(anacea);
    ASSERT_EQ(notes.size(), 2u);
    EXPECT_TRUE(notes[0].compacted);
    EXPECT_FALSE(notes[1].compacted); // The compaction snapshot cannot swallow concurrent arrivals.
    auto activeNotes = store.Notes(anacea, 1024, true);
    ASSERT_EQ(activeNotes.size(), 1u);
    EXPECT_EQ(activeNotes[0].id, notes[1].id);

    EXPECT_FALSE(store.EditNote(anacea, notes[0].id, 1, "The debt was paid.", 2, false));
    ASSERT_TRUE(store.EditNote(anacea, notes[0].id, 1, "Orion clarified that his debt is already paid.", 1, false));
    EXPECT_FALSE(store.EditNote(anacea, notes[0].id, 1, "Restore the debt.", 1, false));
    EXPECT_FALSE(store.Compact(anacea, actor->version, 1, "Restore stale debt recall.", compactInputs));
    actor = store.Actor(anacea);
    ASSERT_TRUE(actor);
    EXPECT_TRUE(actor->recall.empty());
    EXPECT_GT(actor->version, 2u);
    ASSERT_TRUE(store.Extract(anacea, actor->version, "", {note}, {*source}));
    notes = store.Notes(anacea);
    ASSERT_EQ(notes.size(), 2u);
    EXPECT_EQ(notes[0].text, "Orion clarified that his debt is already paid.");
    EXPECT_EQ(notes[0].authority, "owner");
    EXPECT_EQ(store.Observations(anacea, false).size(), 2u);
    actor = store.Actor(anacea);
    ASSERT_TRUE(actor);
    EXPECT_FALSE(store.AddOwnerFact(anacea, actor->version, std::string(run) + ":fact", "I have a sister.",
        2, false, 400000));
    ASSERT_TRUE(store.AddOwnerFact(anacea, actor->version, std::string(run) + ":fact", "I have a sister.",
        1, false, 400000));
    EXPECT_FALSE(store.AddOwnerFact(anacea, actor->version, std::string(run) + ":stale", "I have no family.",
        1, false, 400001));
    EXPECT_EQ(store.Notes(anacea).back().authority, "owner");
    EXPECT_TRUE(store.ResolveNote(anacea, notes[1].id, notes[1].version, 1, false));
    EXPECT_TRUE(store.Notes(anacea)[1].resolved);
    EXPECT_EQ(store.Notes(anacea)[0].sources[0].Key(), source->Key());
    auto spawn = static_cast<uint32_t>(source->id);
    ASSERT_TRUE(store.MapNpc({spawn, 0, 0, npc, watch}));
    ASSERT_TRUE(store.MapNpc({spawn, 0, 0, npc, watch}));
    EXPECT_FALSE(store.MapNpc({spawn, 0, 0, anacea, watch}));
    EXPECT_EQ(store.Npc(spawn, 0, 0)->actorId, npc);
    EXPECT_FALSE(store.Npc(spawn, 0, 99));
    ASSERT_TRUE(store.EnsureActor({anacea, "player", 0, "Anacea"}));
    EXPECT_EQ(store.Actor(anacea)->foundation, "An apprentice priest with no siblings.");
    ASSERT_TRUE(store.EnsureActor({anacea, "bot", 1, "Anacea"}));
    EXPECT_EQ(store.Notes(anacea).size(), 3u); // Human login/recruitment retains the same identity and memories.
}

TEST(PBCStore, AmbiguousDeliveryIsNotHistoryAndRegenerationInvalidatesDerivedRecall)
{
    char const* info = std::getenv("PBC_BUDGET_TEST_INFO");
    char const* run = std::getenv("PBC_BUDGET_TEST_ID");
    if (!info || !run)
        GTEST_SKIP() << "Requires isolated character database";
    MySQL::Library_Init();
    struct Guard { ~Guard() { MySQL::Library_End(); } } guard;
    PBC::CharacterStore store;
    ASSERT_TRUE(store.Open(info));
    std::string actor = std::string(run) + ":delivery";
    ASSERT_TRUE(store.EnsureActor({actor, "bot", 1, "Anacea"}));
    PBC::ObservationRecord event;
    event.eventKey = std::string(run) + ":delivery-first";
    event.sceneId = run;
    event.authorId = actor;
    event.channel = "say";
    event.evidence = "delivered";
    event.text = "I will meet you at the inn.";
    event.createdMs = 100;
    ASSERT_TRUE(store.PrepareDelivery(actor, 1, event, {actor}));
    EXPECT_TRUE(store.Observations(actor, false).empty()); // A crash here is an ambiguous attempt, not a memory.
    EXPECT_FALSE(store.PrepareDelivery(actor, 1, event, {actor})); // Never authorizes retransmission.
    PBC::CharacterStore restarted;
    ASSERT_TRUE(restarted.Open(info));
    EXPECT_TRUE(restarted.Observations(actor, false).empty());
    auto source = store.Observe(event, {actor}); // Called only after the game send succeeds.
    ASSERT_TRUE(source);
    ASSERT_TRUE(store.CloseDelivery(event.eventKey, *source));
    PBC::NoteRecord note;
    note.operationId = std::string(run) + ":delivery-note";
    note.owner = actor;
    note.kind = "memory";
    note.text = "I said I would meet Orion at the inn.";
    note.sources = {*source};
    ASSERT_TRUE(store.Extract(actor, 1, "", {note}, {*source}));
    ASSERT_TRUE(store.Compact(actor, 1, 0, note.text, store.Notes(actor)));
    EXPECT_FALSE(store.ReviseSource(*source, "I said I might visit the inn.", false, false));
    ASSERT_TRUE(store.ReviseSource(*source, "I said I might visit the inn.", false, true));
    EXPECT_FALSE(store.ReviseSource(*source, "A stale second edit.", false, true));
    auto revised = store.Observations(actor, true);
    ASSERT_EQ(revised.size(), 1u);
    EXPECT_EQ(revised[0].source.version, 2u);
    EXPECT_EQ(revised[0].text, "I said I might visit the inn.");
    EXPECT_TRUE(store.Notes(actor).empty());
    EXPECT_TRUE(store.Actor(actor)->recall.empty());
    EXPECT_FALSE(store.Extract(actor, 1, "", {note}, {*source}));
    ASSERT_TRUE(store.ReviseSource(revised[0].source, revised[0].text, true, true));
    EXPECT_TRUE(store.Observations(actor, false).empty()); // Exclusion hides, revision history retains the raw text.
    ASSERT_TRUE(store.Source(actor, source->id)); // Explicit authorized inspection can still recover excluded text.
    EXPECT_EQ(store.Source(actor, source->id)->source.version, 3u);
}
