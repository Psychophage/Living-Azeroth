#include "pbc_guild.h"
#include <gtest/gtest.h>

using namespace PBC;

TEST(PBCGuild, AnUnwrittenIdentityDescribesNothing)
{
    GuildIdentity identity;
    EXPECT_TRUE(identity.Empty());
    EXPECT_TRUE(identity.Describe().empty());
    EXPECT_EQ(identity.reportHours, 72u);
}

TEST(PBCGuild, WrittenFieldsBecomeTheMembersDescription)
{
    GuildIdentity identity;
    std::string error;
    ASSERT_TRUE(ApplyGuildIdentityJson(identity,
        pbc_json{{"purpose", "  Keep the old roads open  "}, {"voice", "Formal; Sir and Dame."}, {"report_hours", 24}},
        error)) << error;
    EXPECT_EQ(identity.purpose, "Keep the old roads open");
    auto text = identity.Describe();
    EXPECT_NE(text.find("What the guild is for: Keep the old roads open."), std::string::npos);
    EXPECT_NE(text.find("How its members speak: Formal; Sir and Dame."), std::string::npos);
    EXPECT_EQ(text.find("Traditions"), std::string::npos);  // unwritten fields are left out
    EXPECT_NE(text.find("not every member's personal experience"), std::string::npos);
    EXPECT_EQ(identity.reportHours, 24u);
}

TEST(PBCGuild, BadChangesChangeNothing)
{
    GuildIdentity identity;
    std::string error;
    ASSERT_TRUE(ApplyGuildIdentityJson(identity, pbc_json{{"values", "Mercy."}}, error));
    EXPECT_FALSE(ApplyGuildIdentityJson(identity, pbc_json{{"values", "Courage."}, {"report_hours", 0}}, error));
    EXPECT_FALSE(ApplyGuildIdentityJson(identity, pbc_json{{"report_hours", 721}}, error));
    EXPECT_FALSE(ApplyGuildIdentityJson(identity, pbc_json{{"purpose", std::string(401, 'x')}}, error));
    EXPECT_FALSE(ApplyGuildIdentityJson(identity, pbc_json{{"motto", "Onward"}}, error));
    EXPECT_FALSE(ApplyGuildIdentityJson(identity, pbc_json{{"purpose", 5}}, error));
    EXPECT_EQ(identity.values, "Mercy.");
    EXPECT_EQ(identity.reportHours, 72u);
}

TEST(PBCGuild, JsonRoundTrips)
{
    GuildIdentity identity;
    std::string error;
    ASSERT_TRUE(ApplyGuildIdentityJson(identity, pbc_json{{"ambitions", "Northrend."}, {"traditions", "Oaths."}}, error));
    GuildIdentity copy;
    ASSERT_TRUE(ApplyGuildIdentityJson(copy, GuildIdentityJson(identity), error)) << error;
    EXPECT_EQ(GuildIdentityJson(copy), GuildIdentityJson(identity));
}
