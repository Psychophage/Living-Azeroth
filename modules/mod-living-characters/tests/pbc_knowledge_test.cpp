// SPDX-License-Identifier: GPL-2.0-or-later
#include "pbc_knowledge.h"
#include "pbc_context.h"
#include <gtest/gtest.h>

namespace
{
pbc_json Catalogue()
{
    return pbc_json::parse(R"({"version":1,"era":"wotlk",
      "sources":{"quest":{"reference":"quest_template:7","revision":"test","kind":"world_db"}},
      "records":[
        {"id":"abbey","layer":"location","text":"Northshire Abbey.","authored":false,"priority":50,
         "sources":["quest"],"when":{"area_id":24}},
        {"id":"vineyard","layer":"location","text":"Northshire Vineyards.","authored":false,"priority":50,
         "sources":["quest"],"when":{"area_id":59}},
        {"id":"mcbride","layer":"npc","text":"Marshal McBride.","authored":false,"priority":100,
         "sources":["quest"],"when":{"entry":197}},
        {"id":"progress","layer":"quest","text":"Your task is underway.","authored":false,"priority":90,
         "sources":["quest"],"when":{"entry":197,"quest_id":7,"quest_states":["incomplete"]}},
        {"id":"turnin","layer":"quest","text":"Objectives fulfilled; reward not given.","authored":false,"priority":90,
         "sources":["quest"],"when":{"entry":197,"quest_id":7,"quest_states":["complete"]}},
        {"id":"rewarded","layer":"quest","text":"You already rewarded this adventurer.","authored":false,"priority":90,
         "sources":["quest"],"when":{"entry":197,"quest_id":7,"quest_states":["rewarded"]}},
        {"id":"watch-secret","layer":"group","text":"Watch-only detail.","authored":true,"priority":80,
         "sources":["quest"],"when":{"groups":["watch:northshire"]}}
      ],"groups":[{"id":"watch:northshire","name":"Northshire watch","description":"Local reports.",
        "when":{"entries":[197,1423],"zone_id":12},"report_lifetime_hours":72,"public_reports":true}]})");
}
}

TEST(PBCKnowledge, LocationAndQuestTransitionsKeepIdentityAndDoNotLeakBetweenPlayers)
{
    PBC::KnowledgeCatalogue catalogue;
    std::string error;
    ASSERT_TRUE(catalogue.Load(Catalogue().dump(), "wotlk", error)) << error;
    auto facts = pbc_json::parse(R"({"entry":197,"zone_id":12,"area_id":24,"interaction_quests":{"7":"incomplete"}})");
    auto before = catalogue.Select(facts, {});
    EXPECT_NE(before.records.dump().find("Your task is underway"), std::string::npos);
    EXPECT_EQ(before.records.dump().find("Watch-only"), std::string::npos);
    facts["interaction_quests"]["7"] = "complete";
    auto complete = catalogue.Select(facts, {});
    EXPECT_NE(complete.records.dump().find("reward not given"), std::string::npos);
    EXPECT_EQ(complete.records.dump().find("already rewarded"), std::string::npos);
    facts["interaction_quests"]["7"] = "rewarded";
    EXPECT_NE(catalogue.Select(facts, {}).records.dump().find("already rewarded"), std::string::npos);
    facts["interaction_quests"]["7"] = "none";
    EXPECT_EQ(catalogue.Select(facts, {}).records.dump().find("already rewarded"), std::string::npos);
    facts["area_id"] = uint32_t{59};
    auto moved = catalogue.Select(facts, {});
    EXPECT_NE(moved.records.dump().find("Northshire Vineyards"), std::string::npos);
    EXPECT_EQ(moved.records.dump().find("Northshire Abbey"), std::string::npos);
    EXPECT_NE(moved.records.dump().find("Marshal McBride"), std::string::npos);
    facts["entry"] = uint32_t{823};
    EXPECT_EQ(catalogue.Select(facts, {}).records.dump().find("Marshal McBride"), std::string::npos);
}

TEST(PBCKnowledge, GroupMembershipAndBoundedWholeFacts)
{
    PBC::KnowledgeCatalogue catalogue;
    std::string error;
    ASSERT_TRUE(catalogue.Load(Catalogue().dump(), "wotlk", error));
    auto facts = pbc_json::parse(R"({"entry":197,"zone_id":12,"area_id":24})");
    ASSERT_EQ(catalogue.Groups(facts).size(), 1u);
    auto selected = catalogue.Select(facts, {"watch:northshire"}, 200);
    EXPECT_LE(selected.bytes, 200u);
    EXPECT_EQ(selected.bytes, selected.records.dump().size());
    EXPECT_FALSE(selected.omitted.empty());
    EXPECT_NE(selected.records.dump().find("Marshal McBride."), std::string::npos);
    facts["zone_id"] = uint32_t{1519};
    EXPECT_TRUE(catalogue.Groups(facts).empty());
    facts["entry"] = uint32_t{0};
    EXPECT_TRUE(catalogue.Groups(facts).empty());
}

TEST(PBCKnowledge, InvalidReplacementLeavesValidatedCatalogueUntouched)
{
    PBC::KnowledgeCatalogue catalogue;
    std::string error;
    auto data = Catalogue();
    ASSERT_TRUE(catalogue.Load(data.dump(), "wotlk", error));
    EXPECT_FALSE(catalogue.Load(data.dump(), "vanilla", error));
    EXPECT_EQ(catalogue.Era(), "wotlk");
    data["records"][0]["sources"] = {"invented-source"};
    EXPECT_FALSE(catalogue.Load(data.dump(), "wotlk", error));
    data = Catalogue();
    data["records"][0]["when"]["quest_states"] = {"complete"};
    EXPECT_FALSE(catalogue.Load(data.dump(), "wotlk", error));
    data = Catalogue();
    data["records"][0]["text"] = std::string(1201, 'x');
    EXPECT_FALSE(catalogue.Load(data.dump(), "wotlk", error));
    EXPECT_FALSE(catalogue.Empty());
}

TEST(PBCKnowledge, SharedReportsNeedCurrentReadAccessAndBoundTheirContext)
{
    PBC::ContextInput input;
    input.actor.id = "speaker";
    input.subjects = {"traveler"};
    input.readableGroups = {"guild:7"};
    input.reportByteBudget = 600;
    for (unsigned i = 0; i < 100; ++i)
    {
        PBC::NoteRecord report;
        report.id = i + 1;
        report.owner = "guild:7";
        report.subject = "traveler";
        report.kind = "report";
        report.text = "A member reported a missing parcel; this is an allegation.";
        report.createdMs = i;
        input.groupReports.push_back(report);
    }
    auto context = PBC::BuildCharacterContext(input);
    ASSERT_TRUE(context.success);
    auto body = pbc_json::parse(context.user);
    EXPECT_LE(body["group_reports"].dump().size(), 600u);
    EXPECT_EQ(body["group_reports"].front()["note_id"], 100);
    EXPECT_TRUE(context.permissions.reportGroups.empty()); // Read access does not grant publication.
    input.readableGroups.clear();
    context = PBC::BuildCharacterContext(input);
    ASSERT_TRUE(context.success);
    EXPECT_TRUE(pbc_json::parse(context.user)["group_reports"].empty());
}
