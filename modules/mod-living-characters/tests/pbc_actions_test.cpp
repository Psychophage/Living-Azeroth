#include "pbc_actions.h"
#include "pbc_action_followup.h"
#include "pbc_action_input.h"

#include <gtest/gtest.h>

namespace
{
PBC::ActionRequest Request(std::string id, uint64_t sequence = 1, PBC::ActionKind kind = PBC::ActionKind::Stay)
{
    PBC::ActionRequest request;
    request.id = std::move(id);
    request.groupId = "human-input";
    request.sequence = sequence;
    request.actor.guid = 41;
    request.requester.guid = 107;
    request.kind = kind;
    request.deadlineMs = 60000;
    return request;
}
}  // namespace

TEST(PBCActions, CurrencyAmountsUseExplicitDenominationsAndExactCopper)
{
    auto amounts =
        PBC::ActionMoneyAmounts("Buy 3 loaves, up to 1 gold 25 silver and 2 copper total; train for 2g each.");
    ASSERT_EQ(amounts.size(), 2u);
    EXPECT_EQ(amounts.at("1 gold 25 silver and 2 copper"), 12502u);
    EXPECT_EQ(amounts.at("2g"), 20000u);
    EXPECT_EQ(PBC::ActionMoneyAmounts("up to 0.25 gold").at("0.25 gold"), 2500u);
    EXPECT_EQ(PBC::ActionMoneyAmounts("zero copper").at("zero copper"), 0u);
    EXPECT_EQ(PBC::ActionMoneyAmounts("ten gold").at("ten gold"), 100000u);
    EXPECT_TRUE(PBC::ActionMoneyAmounts("buy 4, then wait 5 minutes").empty());
    EXPECT_TRUE(PBC::ActionMoneyAmounts("999999999999999999999999 gold").empty());
    EXPECT_TRUE(PBC::ActionMoneyAmounts("0.5 copper").empty());
    EXPECT_TRUE(PBC::ActionMoneyAmounts("-5 gold or .5 gold").empty());
    EXPECT_EQ(PBC::ActionMoneyAmounts("0.0001 gold").at("0.0001 gold"), 1u);
    EXPECT_TRUE(PBC::ActionMoneyAmounts("0.00001 gold").empty());
    EXPECT_TRUE(PBC::ActionMoneyAmounts("2147483647 copper 1 copper").contains("2147483647 copper"));
}

TEST(PBCActions, LateNativeSettlementResolvesUncertainTradeWithoutResumingSequence)
{
    std::vector<PBC::ActionOutcome> outcomes;
    unsigned dispatches = 0;
    PBC::ActionCoordinator coordinator({[](auto const&) { return PBC::ActionStatus::Completed; },
                                        [&](auto const&)
                                        {
                                            ++dispatches;
                                            return true;
                                        },
                                        {},
                                        [&](auto const&, auto const& outcome) { outcomes.push_back(outcome); }});
    ASSERT_TRUE(coordinator.Submit({Request("trade", 1, PBC::ActionKind::Trade), Request("follow")}, 0));
    coordinator.Update(0);
    coordinator.CancelActor(41, "human interrupted", 1);
    EXPECT_EQ(outcomes.front().status, PBC::ActionStatus::Unresolved);
    coordinator.Receive({"trade", PBC::ActionStatus::Completed, "native transfer completed before cancellation", 2});
    EXPECT_EQ(outcomes.back().status, PBC::ActionStatus::Completed);
    auto count = outcomes.size();
    coordinator.Receive({"trade", PBC::ActionStatus::Completed, "duplicate", 3});
    coordinator.Update(4);
    EXPECT_EQ(outcomes.size(), count);
    EXPECT_EQ(dispatches, 1u);
    EXPECT_EQ(coordinator.Active(), 0u);
}

TEST(PBCActions, TwoStepsWaitForNativeCompletionAndNeverReplay)
{
    std::vector<std::string> dispatches;
    std::vector<PBC::ActionOutcome> outcomes;
    PBC::ActionCoordinator coordinator({{},
                                        [&](auto const& request)
                                        {
                                            dispatches.push_back(request.id);
                                            return true;
                                        },
                                        {},
                                        [&](auto const&, auto const& outcome) { outcomes.push_back(outcome); }});
    auto first = Request("first", 1, PBC::ActionKind::Approach);
    auto second = Request("second", 1, PBC::ActionKind::Stay);
    EXPECT_FALSE(coordinator.Submit({first, second, Request("third")}, 0));
    ASSERT_TRUE(coordinator.Submit({first, second}, 0));
    coordinator.Receive({"first", PBC::ActionStatus::Completed, "forged before dispatch", 0});
    coordinator.Update(0);
    coordinator.Receive({"first", PBC::ActionStatus::Started, "moving", 10});
    coordinator.Update(100);
    EXPECT_EQ(dispatches, std::vector<std::string>{"first"});
    coordinator.Receive({"first", PBC::ActionStatus::Completed, "arrival observed", 200});
    coordinator.Receive({"first", PBC::ActionStatus::Completed, "duplicate", 201});
    coordinator.Update(201);
    EXPECT_EQ(dispatches, (std::vector<std::string>{"first", "second"}));
    coordinator.Receive({"second", PBC::ActionStatus::Completed, "stay established", 202});
    EXPECT_EQ(coordinator.Active(), 0u);
    EXPECT_FALSE(coordinator.Submit({first, second}, 300));
    EXPECT_EQ(outcomes.size(), 3u);
}

TEST(PBCActions, RepeatedModelExpenseWithDifferentOperationIdsExecutesOnce)
{
    unsigned prepared = 0, dispatched = 0;
    PBC::ActionCoordinator coordinator({[&](auto const&)
                                        {
                                            ++prepared;
                                            return PBC::ActionStatus::Completed;
                                        },
                                        [&](auto const&)
                                        {
                                            ++dispatched;
                                            return true;
                                        },
                                        {},
                                        {}});
    auto first = Request("buy-first", 1, PBC::ActionKind::Buy);
    first.objectId = 159;
    first.quantity = 3;
    first.target.guid = 123;
    first.reservedCopper = 15;
    auto repeated = first;
    repeated.id = "buy-duplicate";
    ASSERT_TRUE(coordinator.Submit({first, repeated}, 0));
    coordinator.Update(0);
    coordinator.Receive({first.id, PBC::ActionStatus::Completed, "native inventory and wallet changed", 1, 15});
    coordinator.Update(2);
    EXPECT_EQ(prepared, 1u);
    EXPECT_EQ(dispatched, 1u);
    EXPECT_FALSE(coordinator.Active(41));
    repeated.target.guid = 456;
    EXPECT_FALSE(PBC::SameActionIntent(first, repeated));
    repeated = first;
    repeated.quantity = 2;
    EXPECT_FALSE(PBC::SameActionIntent(first, repeated));
}

TEST(PBCActions, FailureStopsDependentActionAndNewOrdersRejectOldResults)
{
    std::vector<std::string> dispatches;
    std::vector<std::string> cancellations;
    std::vector<PBC::ActionOutcome> outcomes;
    PBC::ActionCoordinator coordinator({{},
                                        [&](auto const& request)
                                        {
                                            dispatches.push_back(request.id);
                                            return true;
                                        },
                                        [&](auto const& request) { cancellations.push_back(request.id); },
                                        [&](auto const&, auto const& outcome) { outcomes.push_back(outcome); }});
    ASSERT_TRUE(coordinator.Submit({Request("approach"), Request("buff", 1, PBC::ActionKind::Buff)}, 0));
    coordinator.Update(0);
    coordinator.Receive({"approach", PBC::ActionStatus::Failed, "target disappeared", 1});
    coordinator.Update(2);
    EXPECT_EQ(dispatches.size(), 1u);
    ASSERT_EQ(outcomes.size(), 2u);
    EXPECT_EQ(outcomes.back().detail, "prerequisite_not_completed");
    ASSERT_TRUE(coordinator.Submit({Request("old", 2)}, 10));
    coordinator.Update(10);
    ASSERT_TRUE(coordinator.Submit({Request("new", 3)}, 11));
    coordinator.Receive({"old", PBC::ActionStatus::Completed, "late result", 12});
    EXPECT_FALSE(coordinator.Submit({Request("late inference", 2)}, 13));
    coordinator.Update(14);
    EXPECT_EQ(dispatches.back(), "new");
    EXPECT_EQ(cancellations, std::vector<std::string>{"old"});
}

TEST(PBCActions, SensitiveIntentMustBeDurableAndUncertainWorkIsNeverRetried)
{
    auto prepared = PBC::ActionStatus::Pending;
    unsigned calls = 0;
    std::vector<PBC::ActionOutcome> outcomes;
    PBC::ActionCoordinator coordinator({[&](auto const&) { return prepared; },
                                        [&](auto const&)
                                        {
                                            ++calls;
                                            return true;
                                        },
                                        {},
                                        [&](auto const&, auto const& outcome) { outcomes.push_back(outcome); }});
    ASSERT_TRUE(coordinator.Submit({Request("purchase", 1, PBC::ActionKind::Buy)}, 0));
    coordinator.Update(0);
    EXPECT_EQ(calls, 0u);
    prepared = PBC::ActionStatus::Failed;
    coordinator.Update(1);
    EXPECT_EQ(calls, 0u);
    EXPECT_EQ(outcomes.back().detail, "intent_not_persisted");
    prepared = PBC::ActionStatus::Completed;
    ASSERT_TRUE(coordinator.Submit({Request("purchase2", 2, PBC::ActionKind::Buy)}, 2));
    coordinator.Update(2);
    EXPECT_EQ(calls, 1u);
    coordinator.Update(60000);
    EXPECT_EQ(outcomes.back().status, PBC::ActionStatus::Unresolved);
    EXPECT_FALSE(coordinator.Submit({Request("purchase2", 2, PBC::ActionKind::Buy)}, 60001));
    coordinator.Receive({"purchase2", PBC::ActionStatus::Completed, "late", 60002});
    EXPECT_EQ(outcomes.back().status, PBC::ActionStatus::Completed);
    EXPECT_EQ(calls, 1u);  // Native settlement resolves history without another execution.
}

TEST(PBCActions, IndependentBotsAndShutdownHaveFiniteWork)
{
    unsigned calls = 0;
    PBC::ActionCoordinator coordinator({{},
                                        [&](auto const&)
                                        {
                                            ++calls;
                                            return true;
                                        },
                                        {},
                                        {}});
    auto first = Request("one");
    auto other = Request("two");
    other.actor.guid = 42;
    ASSERT_TRUE(coordinator.Submit({first}, 0));
    ASSERT_TRUE(coordinator.Submit({other}, 0));
    coordinator.Update(0);
    EXPECT_EQ(calls, 2u);
    coordinator.CancelAll("shutdown", 100);
    EXPECT_EQ(coordinator.Active(), 0u);
    coordinator.Update(101);
    EXPECT_EQ(calls, 2u);
}

TEST(PBCActions, TimedWaitDelaysOnlyItsNextStepAndNewOrdersReplaceTheTimer)
{
    std::vector<std::string> dispatches;
    PBC::ActionCoordinator coordinator({{},
                                        [&](auto const& request)
                                        {
                                            dispatches.push_back(request.id);
                                            return true;
                                        },
                                        {},
                                        {}});
    auto stay = Request("timed wait");
    stay.durationMs = 300000;
    ASSERT_TRUE(coordinator.Submit({stay, Request("then follow", 1, PBC::ActionKind::Follow)}, 0));
    coordinator.Update(0);
    coordinator.Receive({stay.id, PBC::ActionStatus::Completed, "hold established", 1});
    coordinator.Update(299999);
    EXPECT_EQ(dispatches.size(), 1u);
    coordinator.Update(300001);
    ASSERT_EQ(dispatches.size(), 2u);
    EXPECT_EQ(dispatches.back(), "then follow");
    auto newer = Request("new stay", 2);
    newer.deadlineMs = 400000;
    ASSERT_TRUE(coordinator.Submit({newer}, 300002));
    coordinator.Receive({"then follow", PBC::ActionStatus::Completed, "late", 300003});
    coordinator.Update(300004);
    EXPECT_EQ(dispatches.back(), "new stay");
}

TEST(PBCActions, ClarifiedTargetReplacesUnresolvedReferenceWithoutChangingAuthority)
{
    PBC::PendingAction pending;
    pending.decisions = {{"intent1", "attack"},
                         {"recipient1", "player:41"},
                         {"target1", "CLARIFY"},
                         {"reference1", "UNRESOLVED"},
                         {"second_action", "NONE"}};
    pending.missing = {"target1", "reference1"};
    PBC::ModelReply answer;
    answer.success = true;
    answer.decisions = {{"pending_answer", "YES"}, {"intent1", "trade"},       {"recipient1", "ALL"},
                        {"target1", "unit:51"},    {"reference1", "SELECTED"}, {"second_action", "YES"}};
    auto merged = pending.Merge(answer);
    EXPECT_EQ(merged.decisions.at("intent1"), "attack");
    EXPECT_EQ(merged.decisions.at("recipient1"), "player:41");
    EXPECT_EQ(merged.decisions.at("target1"), "unit:51");
    EXPECT_EQ(merged.decisions.at("reference1"), "SELECTED");
    EXPECT_EQ(merged.decisions.at("second_action"), "NONE");
}

TEST(PBCActions, ClarificationAnswersCannotChangeBoundIntentActorOrKnownArguments)
{
    PBC::PendingAction pending;
    pending.decisions = {{"intent1", "trade"},
                         {"recipient1", "player:41"},
                         {"item1", "item:159"},
                         {"quantity1", "CLARIFY"},
                         {"second_action", "NONE"}};
    pending.missing = {"quantity1"};
    PBC::ModelReply answer;
    answer.success = true;
    answer.decisions = {{"pending_answer", "YES"}, {"quantity1", "two"},  {"intent1", "attack"},
                        {"recipient1", "ALL"},     {"item1", "item:999"}, {"second_action", "YES"}};
    answer.itemSets["1"] = {{"item:117", 1}, {"item:159", 1}};
    auto merged = pending.Merge(answer);
    EXPECT_TRUE(merged.itemSets.empty());
    EXPECT_EQ(merged.decisions.at("quantity1"), "two");
    EXPECT_EQ(merged.decisions.at("intent1"), "trade");
    EXPECT_EQ(merged.decisions.at("recipient1"), "player:41");
    EXPECT_EQ(merged.decisions.at("item1"), "item:159");
    EXPECT_EQ(merged.decisions.at("second_action"), "NONE");
    answer.decisions["pending_answer"] = "NO";
    EXPECT_EQ(pending.Merge(answer).decisions.at("intent1"), "attack");
    answer.success = false;
    answer.decisions["pending_answer"] = "YES";
    EXPECT_EQ(pending.Merge(answer).decisions.at("recipient1"), "ALL");
}

TEST(PBCActions, PendingClarificationExpiresAndDoesNotCrossAuthorityIncarnationOrAudience)
{
    PBC::PendingAction pending;
    auto actor = Request("pending");
    actor.actor.incarnation = 8;
    actor.actor.map = 1;
    actor.authorityVersion = 4;
    pending.actors = {actor};
    pending.channel = "party";
    pending.missing = {"target1"};
    pending.expiresMs = 120000;
    EXPECT_TRUE(pending.Valid({actor}, "party", 119999));
    EXPECT_FALSE(pending.Valid({actor}, "party", 120000));
    EXPECT_FALSE(pending.Valid({actor}, "say", 10));
    EXPECT_FALSE(pending.Valid({}, "party", 10));
    auto changed = actor;
    changed.actor.incarnation++;
    EXPECT_FALSE(pending.Valid({changed}, "party", 10));
    changed = actor;
    changed.requester.guid++;
    EXPECT_FALSE(pending.Valid({changed}, "party", 10));
    changed = actor;
    changed.authorityVersion++;
    EXPECT_FALSE(pending.Valid({changed}, "party", 10));
    changed = actor;
    changed.actorGroup++;
    EXPECT_FALSE(pending.Valid({changed}, "party", 10));
}

TEST(PBCActions, PendingClarificationCannotRetargetARespawnedOrMissingCreature)
{
    PBC::PendingAction pending;
    PlayerbotDialogue::Entity target{123, 1, 0, 1984, 9};
    pending.targets = {{"unit:123", target}};
    EXPECT_TRUE(pending.TargetsValid({{"unit:123", target}}));
    EXPECT_FALSE(pending.TargetsValid({}));
    target.incarnation++;
    EXPECT_FALSE(pending.TargetsValid({{"unit:123", target}}));
}

TEST(PBCActions, CombatReferencesExpireAndDoNotSurviveAuthorityOrAudienceChanges)
{
    PBC::RecentCombatOrder recent;
    auto actor = Request("reference");
    actor.authorityVersion = 4;
    recent.actors = {actor};
    recent.channel = "say";
    recent.expiresMs = 120000;
    EXPECT_TRUE(recent.Valid({actor}, "say", 119999));
    EXPECT_FALSE(recent.Valid({actor}, "say", 120000));
    EXPECT_FALSE(recent.Valid({actor}, "party", 100));
    EXPECT_FALSE(recent.Valid({}, "say", 100));
    actor.authorityVersion++;
    EXPECT_FALSE(recent.Valid({actor}, "say", 100));
}

TEST(PBCActions, AnUnknownIntentCanBeClarifiedWithoutLosingTheAlreadyIdentifiedTarget)
{
    PBC::PendingAction pending;
    pending.decisions = {
        {"intent1", "CLARIFY"}, {"recipient1", "player:41"}, {"target1", "unit:123"}, {"second_action", "NONE"}};
    pending.missing = {"intent1"};
    PBC::ModelReply answer;
    answer.success = true;
    answer.decisions = {{"pending_answer", "YES"},
                        {"intent1", "attack"},
                        {"recipient1", "ALL"},
                        {"target1", "NONE"},
                        {"second_action", "YES"}};
    auto merged = pending.Merge(answer);
    EXPECT_EQ(merged.decisions.at("intent1"), "attack");
    EXPECT_EQ(merged.decisions.at("recipient1"), "player:41");
    EXPECT_EQ(merged.decisions.at("target1"), "unit:123");
    EXPECT_EQ(merged.decisions.at("second_action"), "NONE");
}

TEST(PBCActions, MixedTradeBindsEachExactEntryBeforeAnyNativeDispatch)
{
    PBC::ActionFrame frame;
    auto request = Request("bundle", 1, PBC::ActionKind::Trade);
    request.requester.guid = 7;
    frame.actors = {request};
    frame.targets.emplace("player:7", request.requester);
    frame.inventory[request.actor.guid] = {{70, 117, 7, 10, 0, "Jerky", false, true},
                                           {71, 159, 4, 11, 0, "Water", false, true}};
    PBC::ModelReply reply;
    reply.success = true;
    reply.decisions = {{"intent1", "trade"},  {"recipient1", "ALL"}, {"target1", "NONE"},
                       {"item1", "item:117"}, {"quantity1", "PAIR"}, {"second_action", "NONE"}};
    reply.itemSets["1"] = {{"item:117", 1}, {"item:159", 1}};
    std::string clarification;
    auto resolved = PBC::ResolveActionDecisions(frame, reply, clarification);
    ASSERT_EQ(resolved.size(), 1u) << clarification;
    ASSERT_EQ(resolved[0].size(), 1u);
    auto const& trade = resolved[0][0];
    EXPECT_EQ(trade.quantity, 2u);
    EXPECT_EQ(trade.tradeQuantities, (std::map<uint32_t, uint32_t>{{117, 1}, {159, 1}}));
    EXPECT_EQ(trade.items.size(), 2u);
    auto substitute = trade;
    substitute.tradeQuantities = {{117, 2}};
    EXPECT_FALSE(PBC::SameActionIntent(trade, substitute));
    reply.itemSets["1"]["item:159"] = 5;
    EXPECT_TRUE(PBC::ResolveActionDecisions(frame, reply, clarification).empty());
    EXPECT_FALSE(clarification.empty());
}

TEST(PBCActions, PartyPullWaitsForEveryPreparedCompanion)
{
    std::vector<std::string> dispatched;
    PBC::ActionCoordinator coordinator({[](auto const&) { return PBC::ActionStatus::Completed; },
                                        [&](auto const& request)
                                        {
                                            dispatched.push_back(request.id);
                                            return true;
                                        },
                                        {},
                                        {}});
    auto first = Request("first-position", 1, PBC::ActionKind::Formation);
    first.objectId = 9;
    auto second = first;
    second.id = "second-position";
    second.actor.guid = 42;
    auto pull = Request("prepared-pull", 1, PBC::ActionKind::Pull);
    pull.actor.guid = 43;
    pull.returnToParty = true;
    pull.preparedParty = {{first.actor, 1}, {second.actor, 1}};
    ASSERT_TRUE(coordinator.Submit({first}, 0));
    ASSERT_TRUE(coordinator.Submit({second}, 0));
    ASSERT_TRUE(coordinator.Submit({pull}, 0));
    coordinator.Update(1);
    ASSERT_EQ(dispatched.size(), 2u);
    coordinator.Receive({first.id, PBC::ActionStatus::Completed, "arrived", 2});
    coordinator.Update(2);
    EXPECT_EQ(dispatched.size(), 2u);
    coordinator.Receive({second.id, PBC::ActionStatus::Completed, "arrived", 3});
    coordinator.Update(3);
    ASSERT_EQ(dispatched.size(), 3u);
    EXPECT_EQ(dispatched.back(), pull.id);
}

TEST(PBCActions, FailedPreparationNeverDispatchesTank)
{
    std::vector<std::string> dispatched;
    std::vector<PBC::ActionOutcome> outcomes;
    PBC::ActionCoordinator coordinator({[](auto const&) { return PBC::ActionStatus::Completed; },
                                        [&](auto const& request)
                                        {
                                            dispatched.push_back(request.id);
                                            return true;
                                        },
                                        {},
                                        [&](auto const&, auto const& outcome) { outcomes.push_back(outcome); }});
    auto position = Request("blocked-position", 1, PBC::ActionKind::Formation);
    position.objectId = 9;
    auto pull = Request("blocked-pull", 1, PBC::ActionKind::Pull);
    pull.actor.guid = 42;
    pull.returnToParty = true;
    pull.preparedParty = {{position.actor, 1}};
    ASSERT_TRUE(coordinator.Submit({position}, 0));
    ASSERT_TRUE(coordinator.Submit({pull}, 0));
    coordinator.Update(1);
    coordinator.Receive({position.id, PBC::ActionStatus::Failed, "no reachable location", 2});
    coordinator.Update(2);
    ASSERT_EQ(dispatched.size(), 1u);
    ASSERT_EQ(outcomes.size(), 2u);
    EXPECT_EQ(outcomes.back().id, pull.id);
    EXPECT_EQ(outcomes.back().detail, "party_preparation_incomplete");
}

TEST(PBCActions, CoordinatedPullIdentityIncludesPinnedParticipantsAndAuthority)
{
    auto first = Request("pull", 1, PBC::ActionKind::Pull);
    auto second = first;
    second.returnToParty = true;
    EXPECT_FALSE(PBC::SameActionIntent(first, second));
    first.returnToParty = true;
    first.preparedParty = {{first.actor, 1}};
    second.preparedParty = {{first.actor, 2}};
    EXPECT_FALSE(PBC::SameActionIntent(first, second));
    second.preparedParty.front().authorityVersion = 1;
    EXPECT_TRUE(PBC::SameActionIntent(first, second));
    second.preparedParty.front().actor.incarnation = 1;
    EXPECT_FALSE(PBC::SameActionIntent(first, second));
}
