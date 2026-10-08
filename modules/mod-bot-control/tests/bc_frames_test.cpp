#include "bc_frames.h"
#include <gtest/gtest.h>

using namespace BotControl;

TEST(BotControlFrames, ShortMessageIsOneFrame)
{
    auto frames = EncodeFrames("a1", R"({"id":1,"op":"hello"})");
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0], R"(1a1:1/1:{"id":1,"op":"hello"})");
    auto frame = ParseFrame(frames[0]);
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->id, "a1");
    EXPECT_EQ(frame->part, 1u);
    EXPECT_EQ(frame->total, 1u);
    EXPECT_EQ(frame->chunk, R"({"id":1,"op":"hello"})");
}

TEST(BotControlFrames, LongMessageSplitsWithinTheWhisperLimitAndJoinsBack)
{
    std::string message(2000, 'x');
    auto frames = EncodeFrames("zz9", message);
    ASSERT_GT(frames.size(), 8u);
    Assembler assembler;
    std::string joined;
    for (std::size_t i = 0; i < frames.size(); ++i)
    {
        EXPECT_LE(frames[i].size() + Prefix.size() + 1, 255u);
        auto frame = ParseFrame(frames[i]);
        ASSERT_TRUE(frame);
        auto result = assembler.Add(*frame, joined);
        EXPECT_EQ(result, i + 1 == frames.size() ? Assembler::Result::Complete : Assembler::Result::Partial);
    }
    EXPECT_EQ(joined, message);
}

TEST(BotControlFrames, RejectsMalformedFrames)
{
    EXPECT_FALSE(ParseFrame("2a:1/1:{}"));         // other version
    EXPECT_FALSE(ParseFrame("1:1/1:{}"));          // no id
    EXPECT_FALSE(ParseFrame("1a-b:1/1:{}"));       // id characters
    EXPECT_FALSE(ParseFrame("1a:2/1:{}"));         // part past total
    EXPECT_FALSE(ParseFrame("1a:0/1:{}"));         // parts count from 1
    EXPECT_FALSE(ParseFrame("1a:1/99:{}"));        // too many parts
    EXPECT_FALSE(ParseFrame("1a:x/1:{}"));         // not a number
    EXPECT_FALSE(ParseFrame(std::string(300, '1')));
}

TEST(BotControlFrames, OutOfOrderOrOversizedMessagesAreDropped)
{
    Assembler assembler;
    std::string joined;
    EXPECT_EQ(assembler.Add(*ParseFrame("1a:2/2:tail"), joined), Assembler::Result::Rejected);
    EXPECT_EQ(assembler.Add(*ParseFrame("1a:1/2:head"), joined), Assembler::Result::Partial);
    // A new message abandons the unfinished one.
    EXPECT_EQ(assembler.Add(*ParseFrame("1b:1/1:{}"), joined), Assembler::Result::Complete);
    EXPECT_EQ(joined, "{}");

    auto frames = EncodeFrames("big", std::string(MaxMessage + 10, 'y'));
    ASSERT_LE(frames.size(), MaxParts);
    Assembler large;
    Assembler::Result last = Assembler::Result::Partial;
    for (auto const& text : frames)
        last = large.Add(*ParseFrame(text), joined);
    EXPECT_EQ(last, Assembler::Result::Rejected);
}
