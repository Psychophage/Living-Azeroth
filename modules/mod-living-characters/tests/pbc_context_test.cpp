#include "pbc_context.h"
#include "pbc_json.h"
#include <gtest/gtest.h>

TEST(PBCContext, SelectorHistoryRespectsWitnessesChannelsAndRevisions)
{
    PBC::ObservationRecord publicLine;
    publicLine.source = {1, 1};
    publicLine.sceneId = "previous";
    publicLine.channel = "say";
    publicLine.text = "Rolock: Willem has work for us.";
    auto privateLine = publicLine;
    privateLine.source = {2, 1};
    privateLine.channel = "whisper";
    privateLine.text = "Zero: a private confidence.";
    auto current = publicLine;
    current.source = {3, 1};
    current.sceneId = "current";
    auto revised = publicLine;
    revised.source = {4, 2};
    auto stale = revised;
    stale.source.version = 1;
    auto heardWithoutHuman = publicLine;
    heardWithoutHuman.source = {5, 1};
    auto history = PBC::BuildSelectionHistory(
        {publicLine, privateLine, current, revised},
        {{"rolock", {publicLine, privateLine, current, stale, heardWithoutHuman}}, {"newcomer", {current}}}, "say",
        "current");
    ASSERT_EQ(history.size(), 1u);
    EXPECT_EQ(history[0].text, publicLine.text);
    EXPECT_EQ(history[0].heardBy, std::vector<std::string>{"rolock"});
}

TEST(PBCContext, ExistingCardPlaceholdersUseCurrentFactsWithoutMutatingStoredIdentity)
{
    PBC::ContextInput input;
    input.actor.id = "player:42";
    input.actor.name = "Anacea";
    input.actor.foundation = "{char_name} is a {char_race} {char_class}, level {char_level}. {memories}";
    input.gameFactsJson = R"({"race":"human","class":"priest","level":20})";
    auto context = PBC::BuildCharacterContext(input);
    ASSERT_TRUE(context.success);
    EXPECT_NE(context.user.find("Anacea is a human priest, level 20."), std::string::npos);
    EXPECT_NE(context.user.find("Use personal_notes and recall"), std::string::npos);
    EXPECT_NE(input.actor.foundation.find("{char_name}"), std::string::npos);
}

TEST(PBCContext, PreservesGenerousHistoryAndSeparatesPersonalNotesFromReports)
{
    PBC::ContextInput input;
    input.actor.id = "anacea";
    input.actor.name = "Anacea";
    input.actor.foundation = "A novice priest, no siblings.";
    input.instructions = "Portray this character.";
    input.subjects = {"orion"};
    for (uint64_t i = 1; i <= 200; ++i)
    {
        PBC::ObservationRecord observation;
        observation.source = {i, 1};
        observation.authorId = "orion";
        observation.channel = "say";
        observation.text = "An ordinary turn in a long discussion about finding work and meeting the local watch.";
        input.observations.push_back(observation);
    }
    PBC::NoteRecord correction;
    correction.owner = "anacea";
    correction.kind = "fact";
    correction.authority = "owner";
    correction.compacted = true;
    correction.text = "My father is alive.";
    input.personalNotes.push_back(correction);
    correction.owner = "another-character";
    correction.text = "Someone else's private secret.";
    input.personalNotes.push_back(correction);
    PBC::NoteRecord report;
    report.kind = "report";
    report.subject = "orion";
    report.text = "Bram reported a settled dispute involving Orion.";
    input.watchReports.push_back(report);
    report.subject = "another-player";
    report.text = "A report about an unrelated person.";
    input.watchReports.push_back(report);

    auto context = PBC::BuildCharacterContext(input);
    ASSERT_TRUE(context.success);
    EXPECT_EQ(context.includedSources.size(), 200u);
    auto body = pbc_json::parse(context.user);
    EXPECT_EQ(body["personal_notes"].size(), 1u);
    EXPECT_EQ(body["group_reports"].size(), 1u);
    EXPECT_EQ(body["observations"].front()["source"], "1:1");
    EXPECT_EQ(body["observations"].back()["source"], "200:1");
    EXPECT_EQ(context.user.find("Someone else's private secret"), std::string::npos);
}

TEST(PBCContext, RollsOldestHistoryAndRetainsNewestEvidence)
{
    PBC::ContextInput input;
    input.actor.id = "anacea";
    input.instructions = "Portray this character.";
    input.contextTokens = 8192;
    for (uint64_t i = 1; i <= 100; ++i)
    {
        PBC::ObservationRecord observation;
        observation.source = {i, 1};
        observation.text = std::string(1000, 'a');
        input.observations.push_back(observation);
    }
    auto context = PBC::BuildCharacterContext(input);
    ASSERT_TRUE(context.success);
    EXPECT_LT(context.includedSources.size(), 100u);
    EXPECT_GT(context.includedSources.size(), 5u);
    EXPECT_EQ(context.includedSources.back().id, 100u);
    EXPECT_TRUE(context.permissions.sources.contains("100:1"));
    EXPECT_FALSE(context.permissions.sources.contains("1:1"));
}

TEST(PBCContext, PrivateRoutingCuesRespectOwnershipCorrectionsAndTextBoundaries)
{
    PBC::ActorRecord actor;
    actor.id = "anacea";
    actor.foundation = "A novice priest who once searched for a lost brother.";
    PBC::NoteRecord corrected;
    corrected.owner = "anacea";
    corrected.authority = "owner";
    corrected.kind = "fact";
    corrected.text = "My brother has returned home; I no longer need to find him.";
    auto foreign = corrected;
    foreign.owner = "stranger";
    foreign.text = "Another person's private secret.";
    auto cues = PBC::BuildSelectionCues(actor, {foreign, corrected});
    EXPECT_EQ(cues.find("Authoritative correction/fact:"), 0u);
    EXPECT_EQ(cues.find("Another person's private secret"), std::string::npos);
    EXPECT_NE(cues.find("lost brother"), std::string::npos);
    actor.foundation = "ééééééé";
    actor.summary.clear();
    auto limited = PBC::BuildSelectionCues(actor, {}, 70);
    EXPECT_LE(limited.size(), 70u);
    EXPECT_NO_THROW(pbc_json({{"private", limited}}).dump());
    PBC::ActorRecord stranger;
    EXPECT_TRUE(PBC::BuildSelectionCues(stranger, {}).empty());
}

TEST(PBCContext, ImmediateReferencesExpireWithoutShrinkingDialogueAndIncludeWitnessedNativeResults)
{
    PBC::ObservationRecord offered;
    offered.source = {90, 1};
    offered.createdMs = 1000;
    offered.channel = "party";
    offered.sceneId = "offer";
    offered.text = "Mira: I can spare two waters.";
    auto old = offered;
    old.source.id = 89;
    old.createdMs = 0;
    auto outcome = offered;
    outcome.source.id = 91;
    outcome.channel = "action";
    outcome.text = "Mira: transfer completed.";
    auto hidden = outcome;
    hidden.source.id = 92;
    hidden.text = "Unwitnessed native result";
    std::vector<PBC::ObservationRecord> anchor = {old, offered, outcome, hidden};
    std::map<std::string, std::vector<PBC::ObservationRecord>> heard = {{"mira", {old, offered, outcome}}};
    auto full = PBC::BuildSelectionHistory(anchor, heard, "party", "new");
    EXPECT_EQ(full.size(), 2u);
    auto refs = PBC::BuildActionReferences(anchor, heard, "party", "new", 120000);
    ASSERT_EQ(refs.size(), 2u);
    EXPECT_EQ(refs.back().text, outcome.text);
    EXPECT_EQ(refs.back().heardBy, std::vector<std::string>{"mira"});
    EXPECT_TRUE(PBC::BuildActionReferences(anchor, heard, "party", "new", 121000).empty());
}
