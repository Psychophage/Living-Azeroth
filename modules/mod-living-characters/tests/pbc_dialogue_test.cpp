#include "pbc_dialogue.h"
#include "pbc_json.h"
#include <gtest/gtest.h>

TEST(PBCDialogue, GroupReportsRequireExplicitDestinationAndCannotBecomePersonalFacts)
{
    PBC::DialoguePermissions permissions;
    permissions.sources = {"1:1"};
    permissions.reportGroups = {"guild:7"};
    auto reply = PBC::ParseDialogue(R"({"segments":[],"notes":[
      {"kind":"report","text":"A member reported a missing parcel.","subject":"","scope":"group:guild:7","sources":["1:1"]},
      {"kind":"report","text":"Copy to outsiders.","subject":"","scope":"group:guild:8","sources":["1:1"]},
      {"kind":"fact","text":"Shared certainty.","subject":"","scope":"group:guild:7","sources":["1:1"]}]})", permissions);
    ASSERT_TRUE(reply);
    ASSERT_EQ(reply->notes.size(), 1u);
    EXPECT_EQ(reply->rejectedNotes, 2u);
    permissions.reportGroups.clear();
    auto departed = PBC::ParseDialogue(R"({"segments":[],"notes":[
      {"kind":"report","text":"Still reporting.","subject":"","scope":"group:guild:7","sources":["1:1"]}]})", permissions);
    ASSERT_TRUE(departed);
    EXPECT_TRUE(departed->notes.empty());
}

TEST(PBCDialogue, PreservesSpeechEmoteSpeechAndSilentResponse)
{
    PBC::DialoguePermissions permissions;
    permissions.animations.insert("wave");
    auto reply = PBC::ParseDialogue(R"({"segments":[
        {"kind":"speech","text":"Orion, wait a moment.","animation":""},
        {"kind":"emote","text":"waves to the stranger.","animation":"wave"},
        {"kind":"speech","text":"We haven't met.","animation":""}],"notes":[]})",
                                    permissions);
    ASSERT_TRUE(reply);
    ASSERT_EQ(reply->segments.size(), 3u);
    EXPECT_EQ(reply->segments[1].kind, PBC::Segment::Kind::Emote);
    EXPECT_EQ(reply->segments[2].text, "We haven't met.");
    reply = PBC::ParseDialogue(R"({"segments":[],"notes":[]})", permissions);
    ASSERT_TRUE(reply);
    EXPECT_TRUE(reply->segments.empty());
}

TEST(PBCDialogue, StopsAtTheFirstRepeatedSegment)
{
    PBC::DialoguePermissions permissions;
    permissions.subjects.insert("player:1");
    permissions.actionOptions = {"approach:human"};
    auto reply = PBC::ParseDialogue(R"({"segments":[
        {"kind":"speech","text":"Greetings, young one.","animation":""},
        {"kind":"speech","text":"Greetings, young one.","animation":""},
        {"kind":"speech","text":"Greetings, young one.","animation":""}],"notes":[
        {"kind":"commitment","text":"I greeted them.","subject":"player:1","scope":"personal","sources":["reply:0"]},
        {"kind":"commitment","text":"I greeted them again.","subject":"player:1","scope":"personal",
         "sources":["reply:2"]}],"actions":[{"option":"approach:human","after_segment":-2}]})",
                                    permissions);
    ASSERT_TRUE(reply);
    ASSERT_EQ(reply->segments.size(), 1u);
    EXPECT_EQ(reply->repeatedSegments, 2u);
    ASSERT_EQ(reply->notes.size(), 1u);
    EXPECT_EQ(reply->notes[0].sources[0], "reply:0");
    ASSERT_EQ(reply->actions.size(), 1u);
    EXPECT_EQ(reply->actions[0].afterSegment, 0);

    // A loop can come back after a different line; nothing after the repeat is spoken.
    reply = PBC::ParseDialogue(R"({"segments":[
        {"kind":"speech","text":"Well met.","animation":""},
        {"kind":"speech","text":"The glade is quiet today.","animation":""},
        {"kind":"speech","text":"Well met.","animation":""},
        {"kind":"speech","text":"Safe travels.","animation":""}],"notes":[]})",
                               permissions);
    ASSERT_TRUE(reply);
    ASSERT_EQ(reply->segments.size(), 2u);
    EXPECT_EQ(reply->segments[1].text, "The glade is quiet today.");
    EXPECT_EQ(reply->repeatedSegments, 2u);

    // The same words as speech and as an emote are different segments.
    permissions.animations.insert("wave");
    reply = PBC::ParseDialogue(R"({"segments":[
        {"kind":"speech","text":"waves.","animation":""},
        {"kind":"emote","text":"waves.","animation":"wave"}],"notes":[]})",
                               permissions);
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->segments.size(), 2u);
}

TEST(PBCDialogue, RejectsRawTruncatedOrUnexpectedOutput)
{
    PBC::DialoguePermissions permissions;
    for (std::string const text : {"Hello", "{\"segments\":[", R"({"segments":[],"notes":[],"command":"trade"})",
                                   R"({"segments":[{"kind":"speech","text":"hello","animation":"dance"}],"notes":[]})"})
        EXPECT_FALSE(PBC::ParseDialogue(text, permissions));
}

TEST(PBCDialogue, RejectsUnwitnessedAndOutOfRangeReplySourcesWithoutLosingSpeech)
{
    PBC::DialoguePermissions permissions;
    permissions.sources.insert("42:1");
    permissions.subjects.insert("player:1");
    auto reply = PBC::ParseDialogue(R"({"segments":[
        {"kind":"speech","text":"I'll remember what you said.","animation":""}],"notes":[
        {"kind":"memory","text":"Orion told me about his debt.","subject":"player:1",
         "scope":"personal","sources":["42:1"]},
        {"kind":"memory","text":"I heard their private whisper.","subject":"player:1",
         "scope":"personal","sources":["99:1"]},
        {"kind":"commitment","text":"I promised to help.","subject":"player:1",
         "scope":"personal","sources":["reply:0"]},
        {"kind":"commitment","text":"I promised to fight.","subject":"player:1",
         "scope":"personal","sources":["reply:1"]},
        {"kind":"report","text":"Orion is in debt.","subject":"player:1",
         "scope":"watch","sources":["42:1"]}]})",
                                    permissions);
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->segments.size(), 1u);
    ASSERT_EQ(reply->notes.size(), 2u);
    EXPECT_EQ(reply->rejectedNotes, 3u);
    EXPECT_EQ(reply->notes[1].sources[0], "reply:0");  // Still needs server delivery before acceptance.
}

TEST(PBCDialogue, PersonalActionsAreBoundedAndInvalidProposalsKeepSpeech)
{
    PBC::DialoguePermissions permissions;
    permissions.actionOptions = {"heal:self", "approach:human"};
    auto reply = PBC::ParseDialogue(R"({"segments":[
        {"kind":"speech","text":"Let me help.","animation":""}],"notes":[],"actions":[
        {"option":"heal:self","after_segment":0},
        {"option":"attack:unlisted","after_segment":-1}]})",
                                    permissions);
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->segments.size(), 1u);
    EXPECT_TRUE(reply->actions.empty());
    EXPECT_EQ(reply->rejectedActions, 1u);
    reply = PBC::ParseDialogue(R"({"segments":[],"notes":[],"actions":[
        {"option":"heal:self","after_segment":0},
        {"option":"approach:human","after_segment":-1}]})",
                               permissions);
    ASSERT_TRUE(reply);
    EXPECT_TRUE(reply->actions.empty());  // No second action after a rejected prerequisite.
    reply = PBC::ParseDialogue(R"({"segments":[],"notes":[],"actions":[
        {"option":"heal:self","after_segment":-1},
        {"option":"heal:self","after_segment":-1},
        {"option":"approach:human","after_segment":-1}]})",
                               permissions);
    ASSERT_TRUE(reply);
    EXPECT_TRUE(reply->actions.empty());
}

TEST(PBCDialogue, CombatResponseCannotExpandIntoNormalPlayback)
{
    PBC::DialoguePermissions permissions;
    permissions.maxSegments = 1;
    EXPECT_TRUE(PBC::ParseDialogue(
        R"({"segments":[{"kind":"speech","text":"Keep your guard up!","animation":""}],"notes":[]})", permissions));
    EXPECT_FALSE(PBC::ParseDialogue(
        R"({"segments":[{"kind":"speech","text":"First line.","animation":""},{"kind":"emote","text":"gestures.","animation":""}],"notes":[]})",
        permissions));
    EXPECT_TRUE(PBC::ParseDialogue(R"({"segments":[],"notes":[]})", permissions));
    EXPECT_NE(PBC::DialogueSchema({}, {}, 1).find("\"maxItems\":1"), std::string::npos);
}

TEST(PBCDialogue, SafeNearLimitTextKeepsReplyIndicesButOversizeStillFails)
{
    PBC::DialoguePermissions permissions;
    auto body = [](std::string const& kind, std::size_t length)
    {
        return "{\"segments\":[{\"kind\":\"" + kind + "\",\"text\":\"" + std::string(length, 'a') +
               "\",\"animation\":\"\"}],\"notes\":[{\"kind\":\"memory\",\"text\":\"I answered.\","
               "\"subject\":\"\",\"scope\":\"personal\",\"sources\":[\"reply:0\"]}]}";
    };
    auto reply = PBC::ParseDialogue(body("speech", 237), permissions);
    ASSERT_TRUE(reply);
    ASSERT_EQ(reply->segments.size(), 1u);
    ASSERT_EQ(reply->notes.size(), 1u);
    EXPECT_EQ(reply->notes[0].sources[0], "reply:0");
    EXPECT_TRUE(PBC::ParseDialogue(body("speech", 255), permissions));
    EXPECT_FALSE(PBC::ParseDialogue(body("speech", 256), permissions));
    EXPECT_TRUE(PBC::ParseDialogue(body("emote", 253), permissions));
    EXPECT_FALSE(PBC::ParseDialogue(body("emote", 254), permissions));
}

TEST(PBCDialogue, ExplicitAbstentionNeverExecutesOrRescuesAnInvalidSequence)
{
    PBC::DialoguePermissions permissions;
    permissions.actionOptions = {"heal:human"};
    auto reply = PBC::ParseDialogue(R"({"segments":[],"notes":[],"actions":[
        {"option":"NONE","after_segment":-1}]})",
                                    permissions);
    ASSERT_TRUE(reply);
    EXPECT_TRUE(reply->actions.empty());
    EXPECT_EQ(reply->rejectedActions, 0u);
    reply = PBC::ParseDialogue(R"({"segments":[],"notes":[],"actions":[
        {"option":"NONE","after_segment":-1},{"option":"heal:human","after_segment":0}]})",
                               permissions);
    ASSERT_TRUE(reply);
    EXPECT_TRUE(reply->actions.empty());
    EXPECT_EQ(reply->rejectedActions, 1u);
    auto schema = pbc_json::parse(PBC::DialogueSchema({}, {"heal:human"}, 6));
    EXPECT_EQ(schema["properties"]["actions"]["minItems"], 0);
    EXPECT_TRUE(
        schema["properties"]["actions"]["items"]["properties"]["option"]["enum"].get<std::vector<std::string>>() ==
        (std::vector<std::string>{"heal:human", "NONE"}));
}

TEST(PBCDialogue, LastSegmentTimingUsesActualSpeechAndPreservesSequenceOrder)
{
    PBC::DialoguePermissions permissions;
    permissions.actionOptions = {"heal:human", "offer:159"};
    auto reply = PBC::ParseDialogue(R"({"segments":[
        {"kind":"speech","text":"Let me tend that arm.","animation":""},
        {"kind":"speech","text":"I can offer water too.","animation":""}],"notes":[],"actions":[
        {"option":"heal:human","after_segment":-2},{"option":"offer:159","after_segment":0}]})",
                                    permissions);
    ASSERT_TRUE(reply);
    ASSERT_EQ(reply->actions.size(), 2u);
    EXPECT_EQ(reply->actions[0].afterSegment, 1);
    EXPECT_EQ(reply->actions[1].afterSegment, 1);
    reply = PBC::ParseDialogue(R"({"segments":[],"notes":[],"actions":[
        {"option":"heal:human","after_segment":-2}]})",
                               permissions);
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->rejectedActions, 1u);
    EXPECT_TRUE(reply->actions.empty());
}
