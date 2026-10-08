#include "pbc_listener.h"
#include <gtest/gtest.h>

using namespace PBC;

TEST(PBCListener, DefaultsAreTheRealmsBehaviour)
{
    ListenerSettings settings;
    auto json = ListenerJson(settings);
    EXPECT_EQ(json["party"], "chatty");
    EXPECT_EQ(json["nearby"], "chatty");
    EXPECT_EQ(json["guild"], "chatty");
    EXPECT_EQ(json["general"], "chatty");
    EXPECT_EQ(json["remarks"], "sometimes");
    EXPECT_EQ(json["banter"], "sometimes");
    EXPECT_EQ(json["reading"], 0);
    EXPECT_EQ(json["turns"], 0);
}

TEST(PBCListener, ChangesOnlyWhatIsGivenAndRoundTrips)
{
    ListenerSettings settings;
    std::string error;
    ASSERT_TRUE(ApplyListenerJson(settings,
        pbc_json{{"nearby", "silent"}, {"party", "spoken"}, {"remarks", "never"}, {"reading", 200}, {"turns", 2}},
        error)) << error;
    EXPECT_EQ(settings.On(ListenChannel::Nearby), Hearing::Silent);
    EXPECT_EQ(settings.On(ListenChannel::Party), Hearing::SpokenTo);
    EXPECT_EQ(settings.On(ListenChannel::Guild), Hearing::Chatty);
    EXPECT_EQ(settings.remarks, Remarks::Never);
    EXPECT_EQ(settings.readingWordsPerMinute, 200);
    EXPECT_EQ(settings.maxTurns, 2);

    ListenerSettings copy;
    ASSERT_TRUE(ApplyListenerJson(copy, ListenerJson(settings), error)) << error;
    EXPECT_EQ(ListenerJson(copy), ListenerJson(settings));
}

TEST(PBCListener, ABadFieldChangesNothing)
{
    ListenerSettings settings;
    std::string error;
    EXPECT_FALSE(ApplyListenerJson(settings, pbc_json{{"nearby", "silent"}, {"party", "loud"}}, error));
    EXPECT_EQ(settings.On(ListenChannel::Nearby), Hearing::Chatty);
    EXPECT_FALSE(ApplyListenerJson(settings, pbc_json{{"reading", 1000}}, error));
    EXPECT_FALSE(ApplyListenerJson(settings, pbc_json{{"reading", 30}}, error));
    EXPECT_FALSE(ApplyListenerJson(settings, pbc_json{{"turns", 7}}, error));
    EXPECT_FALSE(ApplyListenerJson(settings, pbc_json{{"whispers", "silent"}}, error));
    EXPECT_FALSE(ApplyListenerJson(settings, pbc_json::array(), error));
    EXPECT_EQ(ListenerJson(settings), ListenerJson(ListenerSettings{}));
}

TEST(PBCListener, AudienceLabelsMapToChannelsAndWhispersAlwaysReach)
{
    EXPECT_EQ(ChannelOfAudience("party"), ListenChannel::Party);
    EXPECT_EQ(ChannelOfAudience("raid"), ListenChannel::Party);
    EXPECT_EQ(ChannelOfAudience("say"), ListenChannel::Nearby);
    EXPECT_EQ(ChannelOfAudience("yell"), ListenChannel::Nearby);
    EXPECT_EQ(ChannelOfAudience("emote"), ListenChannel::Nearby);
    EXPECT_EQ(ChannelOfAudience("guild"), ListenChannel::Guild);
    EXPECT_EQ(ChannelOfAudience("general"), ListenChannel::General);
    EXPECT_FALSE(ChannelOfAudience("whisper"));
}
