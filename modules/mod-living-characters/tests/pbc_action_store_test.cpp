#include "pbc_action_store.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <future>

#include "MySQLThreading.h"

TEST(PBCActionStore, SharedCurrencyCapSurvivesConcurrentRequestsAndUncertainRestart)
{
    auto info = std::getenv("PBC_BUDGET_TEST_INFO");
    auto run = std::getenv("PBC_BUDGET_TEST_ID");
    if (!info || !run)
        GTEST_SKIP() << "Requires the disposable action journal database";
    MySQL::Library_Init();
    struct Guard
    {
        ~Guard() { MySQL::Library_End(); }
    } guard;
    PBC::ActionStore first, second;
    ASSERT_TRUE(first.Open(info));
    ASSERT_TRUE(second.Open(info));
    PBC::ActionRequest request;
    request.id = std::string(run) + ":repair-a";
    request.groupId = run;
    request.kind = PBC::ActionKind::Repair;
    request.actor.guid = 41;
    request.requester.guid = 107;
    request.sequence = 1;
    request.moneyBudget = std::string(run) + ":currency";
    request.copperLimit = 100;
    request.reservedCopper = 70;
    auto other = request;
    other.id = std::string(run) + ":repair-b";
    other.actor.guid = 42;
    auto a = std::async(std::launch::async, [&] { return first.Prepare(request, run, 100); });
    auto b = std::async(std::launch::async, [&] { return second.Prepare(other, run, 100); });
    bool acceptedA = a.get(), acceptedB = b.get();
    EXPECT_NE(acceptedA, acceptedB);
    auto winner = acceptedA ? request : other;
    auto retry = acceptedA ? other : request;
    auto balance = first.MoneyBalance(request.moneyBudget);
    ASSERT_TRUE(balance);
    EXPECT_EQ(balance->held, 70u);
    EXPECT_EQ(balance->spent, 0u);
    ASSERT_TRUE(first.Recover(std::string(run) + ":restart", 200));
    EXPECT_FALSE(second.Prepare(retry, run, 201));
    EXPECT_EQ(second.MoneyBalance(request.moneyBudget)->held, 70u);
    // A known partial expense settles exactly once, including a native learning failure.
    ASSERT_TRUE(first.Record({winner.id, PBC::ActionStatus::Failed, "learning_hook_refused", 210, 20}, 210));
    ASSERT_TRUE(first.Record({winner.id, PBC::ActionStatus::Failed, "duplicate", 210, 20}, 210));
    balance = second.MoneyBalance(request.moneyBudget);
    ASSERT_TRUE(balance);
    EXPECT_EQ(balance->spent, 20u);
    EXPECT_EQ(balance->held, 0u);
    ASSERT_TRUE(second.Prepare(retry, run, 211));
    EXPECT_EQ(first.MoneyBalance(request.moneyBudget)->held, 70u);
    auto changedCap = request;
    changedCap.id = std::string(run) + ":changed-cap";
    changedCap.copperLimit = 1000;
    EXPECT_FALSE(first.Prepare(changedCap, run, 212));
    ASSERT_TRUE(second.Record({retry.id, PBC::ActionStatus::Cancelled, "no_native_execution", 220}, 220));
    EXPECT_EQ(first.MoneyBalance(request.moneyBudget)->held, 0u);
    EXPECT_EQ(first.MoneyBalance(request.moneyBudget)->spent, 20u);
}

TEST(PBCActionStore, DurableIntentCannotAuthorizeDuplicateDispatchAndRestartNeverReplays)
{
    auto info = std::getenv("PBC_BUDGET_TEST_INFO");
    auto run = std::getenv("PBC_BUDGET_TEST_ID");
    if (!info || !run)
        GTEST_SKIP() << "Requires the disposable action journal database";
    MySQL::Library_Init();
    struct LibraryGuard
    {
        ~LibraryGuard() { MySQL::Library_End(); }
    } libraryGuard;
    PBC::ActionStore first;
    PBC::ActionStore second;
    ASSERT_TRUE(first.Open(info));
    ASSERT_TRUE(second.Open(info));
    PBC::ActionRequest request;
    request.id = std::string(run) + ":trade";
    request.groupId = run;
    request.actor.guid = 41;
    request.requester.guid = 107;
    request.sequence = 1;
    request.kind = PBC::ActionKind::Trade;
    request.quantity = 3;
    request.objectId = 2589;
    ASSERT_TRUE(first.Prepare(request, run, 100));
    EXPECT_EQ(second.Status(request.id), PBC::ActionStatus::Pending);
    EXPECT_FALSE(second.Prepare(request, run, 101));
    ASSERT_TRUE(first.Record({request.id, PBC::ActionStatus::Started, "native_trade_open", 110}, 110));
    EXPECT_EQ(second.Status(request.id), PBC::ActionStatus::Started);
    ASSERT_TRUE(second.Recover(std::string(run) + ":restart", 200));
    EXPECT_EQ(first.Status(request.id), PBC::ActionStatus::Unresolved);
    EXPECT_FALSE(first.Prepare(request, run, 201));
    EXPECT_FALSE(first.Record({request.id, PBC::ActionStatus::Started, "retry_forbidden", 202}, 202));
    // A later verified outcome may settle uncertainty; it still cannot authorize replay.
    ASSERT_TRUE(first.Record({request.id, PBC::ActionStatus::Completed, "transfer_observed", 210}, 210));
    EXPECT_FALSE(second.Record({request.id, PBC::ActionStatus::Failed, "late_failure", 211}, 211));
    EXPECT_EQ(second.Status(request.id), PBC::ActionStatus::Completed);
    EXPECT_FALSE(second.Prepare(request, run, 212));
}
