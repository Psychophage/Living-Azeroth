#include "pbc_conversation.h"
#include <gtest/gtest.h>

namespace
{
PBC::Dialogue ThreeSegments()
{
    PBC::Dialogue reply;
    reply.segments = {{PBC::Segment::Kind::Speech, "Hello.", ""},
        {PBC::Segment::Kind::Emote, "pauses thoughtfully.", ""},
        {PBC::Segment::Kind::Speech, "I'll help with your debt.", ""}};
    reply.notes = {{"memory", "Orion described his debt.", "orion", "personal", {"1:1"}},
        {"commitment", "I promised to help.", "orion", "personal", {"reply:2"}}};
    return reply;
}
}

TEST(PBCConversation, PacesSegmentsAndDoesNotRememberUndeliveredPromise)
{
    PBC::Conversation scene;
    scene.SetSpacing(1000);
    scene.SetReadingWordsPerMinute(0);
    ASSERT_TRUE(scene.Begin(PBC::Contribution::Human, true, {"anacea"}, 3, 0));
    ASSERT_TRUE(scene.Choose(scene.Version(), "anacea", 1));
    ASSERT_TRUE(scene.Accept(scene.Version(), 1, ThreeSegments(), "request", 0));
    ASSERT_TRUE(scene.Due(0));
    ASSERT_TRUE(scene.Delivered(scene.Version(), 0, 0));
    EXPECT_FALSE(scene.Due(999));
    EXPECT_TRUE(scene.Due(1000));
    scene.Abort(); // Actor moved, entered combat, or lost the required audience.
    EXPECT_FALSE(scene.Due(10000));
    EXPECT_EQ(scene.DeliveryOutcome(), "partial");
    EXPECT_EQ(scene.ReadyNotes(), std::vector<std::size_t>{0});
    ASSERT_TRUE(scene.Persisted(scene.Version()));
    EXPECT_EQ(scene.Phase(), PBC::ConversationPhase::Done);
}

TEST(PBCConversation, NewInputDiscardsOldReplyAndPlayback)
{
    PBC::Conversation scene;
    ASSERT_TRUE(scene.Begin(PBC::Contribution::Human, true, {"anacea"}, 3, 0));
    auto oldVersion = scene.Version();
    ASSERT_TRUE(scene.Choose(oldVersion, "anacea", 1));
    ASSERT_TRUE(scene.Begin(PBC::Contribution::Human, true, {"anacea"}, 3, 100));
    EXPECT_FALSE(scene.Accept(oldVersion, 1, ThreeSegments(), "stale", 101));
    EXPECT_EQ(scene.Phase(), PBC::ConversationPhase::Selecting);
    ASSERT_TRUE(scene.Choose(scene.Version(), "anacea", 2));
    EXPECT_FALSE(scene.Accept(scene.Version(), 1, ThreeSegments(), "old-fact", 101));
    EXPECT_FALSE(scene.Due(10000));
}

TEST(PBCConversation, GeneratedMessagesCannotRenewAllowance)
{
    PBC::Conversation scene;
    scene.SetSpacing(1000);
    scene.SetReadingWordsPerMinute(0);
    EXPECT_FALSE(scene.Begin(PBC::Contribution::Ambient, false, {"guard"}, 3, 0));
    ASSERT_TRUE(scene.Begin(PBC::Contribution::Ambient, true, {"guard", "anacea"}, 3, 0));
    for (unsigned turn = 0; turn < 3; ++turn)
    {
        if (turn)
            EXPECT_FALSE(scene.Choose(scene.Version(), turn % 2 ? "guard" : "anacea", 1));
        ASSERT_TRUE(scene.Choose(scene.Version(), turn % 2 ? "anacea" : "guard", 1));
        PBC::Dialogue reply;
        reply.segments = {{PBC::Segment::Kind::Speech, "I can help.", ""}};
        ASSERT_TRUE(scene.Accept(scene.Version(), 1, reply, "answer", turn * 1000));
        ASSERT_TRUE(scene.Delivered(scene.Version(), 0, turn * 1000));
        EXPECT_FALSE(scene.Begin(PBC::Contribution::Generated, true, {"guard"}, 3, 0));
        ASSERT_TRUE(scene.Persisted(scene.Version()));
    }
    EXPECT_EQ(scene.Remaining(), 0u);
    EXPECT_EQ(scene.Phase(), PBC::ConversationPhase::Done);
    EXPECT_FALSE(scene.Choose(scene.Version(), "guard", 1));
}

TEST(PBCConversation, SilenceEndsExchangeInsteadOfReactivatingPreviousSpeaker)
{
    PBC::Conversation scene;
    ASSERT_TRUE(scene.Begin(PBC::Contribution::Human, true, {"rolock", "anacea"}, 4, 0));
    ASSERT_TRUE(scene.Choose(scene.Version(), "rolock", 1));
    ASSERT_TRUE(scene.Accept(scene.Version(), 1, {}, "declined", 0));
    ASSERT_TRUE(scene.Persisted(scene.Version()));
    EXPECT_EQ(scene.Phase(), PBC::ConversationPhase::Done);
    EXPECT_EQ(scene.Remaining(), 3u);
    EXPECT_FALSE(scene.Choose(scene.Version(), "anacea", 1));
}

TEST(PBCConversation, SingleSpeakerFinishesAfterOneCompleteReplyWhileHumanRemains)
{
    PBC::Conversation scene;
    scene.SetSpacing(1000);
    scene.SetReadingWordsPerMinute(0);
    ASSERT_TRUE(scene.Begin(PBC::Contribution::Human, true, {"rolock"}, 4, 0));
    ASSERT_TRUE(scene.Choose(scene.Version(), "rolock", 1));
    ASSERT_TRUE(scene.Accept(scene.Version(), 1, ThreeSegments(), "greeting", 0));
    for (std::size_t index = 0; index < 3; ++index)
    {
        ASSERT_TRUE(scene.Due(index * 1000));
        ASSERT_TRUE(scene.Delivered(scene.Version(), index, index * 1000));
    }
    ASSERT_TRUE(scene.Persisted(scene.Version()));
    EXPECT_EQ(scene.DeliveryOutcome(), "delivered");
    EXPECT_EQ(scene.Remaining(), 3u);
    EXPECT_EQ(scene.Phase(), PBC::ConversationPhase::Done);
    EXPECT_FALSE(scene.Choose(scene.Version(), "rolock", 1));
    EXPECT_FALSE(scene.Due(300000));
    // A genuinely new human contribution still starts a fresh exchange.
    ASSERT_TRUE(scene.Begin(PBC::Contribution::Human, true, {"rolock"}, 4, 300000));
    EXPECT_TRUE(scene.Choose(scene.Version(), "rolock", 1));
}

TEST(PBCConversation, ReadingTimeDependsOnLengthAndSurvivesSpeakerChange)
{
    PBC::Conversation scene;
    scene.SetSpacing(1000);
    scene.SetReadingWordsPerMinute(240);
    ASSERT_TRUE(scene.Begin(PBC::Contribution::Human, true, {"rolock", "anacea"}, 4, 0));
    ASSERT_TRUE(scene.Choose(scene.Version(), "rolock", 1));
    PBC::Dialogue reply;
    reply.segments = {{PBC::Segment::Kind::Speech, "Hello there.", ""},
        {PBC::Segment::Kind::Emote, "takes a moment to examine the stranger before offering a smile.", ""}};
    ASSERT_TRUE(scene.Accept(scene.Version(), 1, reply, "greeting", 0));
    ASSERT_TRUE(scene.Delivered(scene.Version(), 0, 0));
    EXPECT_FALSE(scene.Due(1499)); // Two words at 240 wpm plus one second to settle.
    ASSERT_TRUE(scene.Due(1500));
    ASSERT_TRUE(scene.Delivered(scene.Version(), 1, 1500));
    ASSERT_TRUE(scene.Persisted(scene.Version()));
    ASSERT_TRUE(scene.Choose(scene.Version(), "anacea", 1));
    ASSERT_TRUE(scene.Accept(scene.Version(), 1, ThreeSegments(), "follow-up", 1600));
    EXPECT_FALSE(scene.Due(5249)); // Eleven-word emote needs longer; the next speaker must wait too.
    ASSERT_TRUE(scene.Due(5250));
    scene.Abort();
    EXPECT_FALSE(scene.Due(100000));
    ASSERT_TRUE(scene.Begin(PBC::Contribution::Human, true, {"anacea"}, 4, 1700));
    ASSERT_TRUE(scene.Choose(scene.Version(), "anacea", 1));
    ASSERT_TRUE(scene.Accept(scene.Version(), 1, ThreeSegments(), "interruption", 1700));
    EXPECT_TRUE(scene.Due(1700)); // A human interruption replaces the old schedule.
}

TEST(PBCConversation, SlowDefaultsDoNotCapLongReadingAndNewInputInterrupts)
{
    PBC::Conversation scene;
    ASSERT_TRUE(scene.Begin(PBC::Contribution::Human, true, {"orion", "anacea"}, 4, 0));
    ASSERT_TRUE(scene.Choose(scene.Version(), "orion", 1));
    PBC::Dialogue reply;
    std::string text;
    for (unsigned word = 0; word < 50; ++word)
        text += "a ";
    reply.segments = {{PBC::Segment::Kind::Speech, text, ""}};
    ASSERT_TRUE(scene.Accept(scene.Version(), 1, reply, "long", 0));
    ASSERT_TRUE(scene.Delivered(scene.Version(), 0, 0));
    ASSERT_TRUE(scene.Persisted(scene.Version()));
    ASSERT_TRUE(scene.Choose(scene.Version(), "anacea", 1));
    ASSERT_TRUE(scene.Accept(scene.Version(), 1, ThreeSegments(), "next", 1000));
    EXPECT_FALSE(scene.Due(15000)); // Fifty words need twenty seconds plus the two-second pause.
    EXPECT_FALSE(scene.Due(21999));
    EXPECT_TRUE(scene.Due(22000));
    ASSERT_TRUE(scene.Begin(PBC::Contribution::Human, true, {"anacea"}, 4, 16000));
    ASSERT_TRUE(scene.Choose(scene.Version(), "anacea", 1));
    ASSERT_TRUE(scene.Accept(scene.Version(), 1, ThreeSegments(), "new-input", 16000));
    EXPECT_TRUE(scene.Due(16000));
}
