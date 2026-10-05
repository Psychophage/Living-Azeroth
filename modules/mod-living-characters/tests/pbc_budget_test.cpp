// SPDX-License-Identifier: GPL-2.0-or-later
// PBC Character System changes, 2026-09-30; upstream attribution in NOTICE.md.

#include "MySQLConnection.h"
#include "MySQLThreading.h"
#include "pbc_budget.h"
#include "pbc_dialogue.h"
#include "pbc_model.h"
#include "pbc_store.h"
#include <cstdlib>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace
{
class FailureFixtureConnection final : public MySQLConnection
{
public:
    using MySQLConnection::MySQLConnection;
    void DoPrepareStatements() override {}
};
}  // namespace

TEST(PBCBudget, DatabaseErrorsPreventHttpAndPreservePendingMemory)
{
    char const* connectionInfo = std::getenv("PBC_BUDGET_TEST_INFO");
    char const* run = std::getenv("PBC_BUDGET_TEST_ID");
    if (!connectionInfo || !run)
        GTEST_SKIP() << "Requires isolated failure fixture";
    MySQL::Library_Init();
    struct Guard
    {
        ~Guard() { MySQL::Library_End(); }
    } guard;
    MySQLConnectionInfo info(connectionInfo);
    FailureFixtureConnection admin(info);
    ASSERT_EQ(admin.Open(), 0u);
    std::string actor = std::string(run) + ":db-failure";
    std::string budget = std::string(run) + ":db-failure";
    ASSERT_TRUE(
        admin.Execute("INSERT INTO pbc_api_budget (budget_id,ceiling_nano) VALUES ('" + budget + "',100000000)"));
    struct Triggers
    {
        FailureFixtureConnection& admin;
        ~Triggers()
        {
            admin.Execute("DROP TRIGGER IF EXISTS pbc_test_reservation_failure");
            admin.Execute("DROP TRIGGER IF EXISTS pbc_test_note_failure");
        }
    } cleanup{admin};
    ASSERT_TRUE(
        admin.Execute("CREATE TRIGGER pbc_test_reservation_failure BEFORE INSERT "
                      "ON pbc_api_request "
                      "FOR EACH ROW BEGIN IF NEW.actor_id='" +
                      actor +
                      "' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='Isolated "
                      "reservation failure'; END IF; END"));
    PBC::BudgetStore ledger;
    ASSERT_TRUE(ledger.Open(connectionInfo, budget));
    PBC::ModelSettings settings;
    settings.apiKey = "recorded-no-network";
    unsigned httpCalls = 0;
    PBC::ModelGateway model(settings, ledger,
                            [&](auto const&, auto const&, auto const&, uint32_t)
                            {
                                ++httpCalls;
                                return PBC_HttpResponse{200, R"({"usage":{"cost":0},"choices":[{"finish_reason":"stop",
            "message":{"content":"{\"segments\":[],\"notes\":[]}"}}]})"};
                            });
    PBC::ApiReservation request;
    request.actorId = actor;
    request.reason = "evaluation";
    EXPECT_FALSE(model.Generate("roleplay", "hello", PBC::DialogueSchema(), request).success);
    EXPECT_EQ(httpCalls, 0u);
    ASSERT_TRUE(admin.Execute("DROP TRIGGER pbc_test_reservation_failure"));
    ASSERT_TRUE(model.Generate("roleplay", "hello", PBC::DialogueSchema(), request).success);
    EXPECT_EQ(httpCalls, 1u);
    ASSERT_TRUE(ledger.Totals());
    EXPECT_EQ(ledger.Totals()->heldNano, 0u);
    // Combat openings use the same real ledger admission and background floor.
    request.reason = "combat_banter";
    request.background = true;
    ASSERT_TRUE(model.Generate("roleplay", "an ongoing fight", PBC::DialogueSchema(), request).success);
    EXPECT_EQ(httpCalls, 2u);
    EXPECT_EQ(ledger.Totals()->heldNano, 0u);

    PBC::CharacterStore store;
    ASSERT_TRUE(store.Open(connectionInfo));
    ASSERT_TRUE(store.EnsureActor({actor, "bot", 1, "Anacea"}));
    PBC::ObservationRecord event;
    event.eventKey = actor;
    event.sceneId = run;
    event.authorId = "player:1";
    event.channel = "say";
    event.text = "Orion promised to return tomorrow.";
    auto source = store.Observe(event, {actor});
    ASSERT_TRUE(source);
    PBC::NoteRecord note;
    note.owner = actor;
    note.operationId = actor;
    note.kind = "commitment";
    note.text = event.text;
    note.sources = {*source};
    ASSERT_TRUE(
        admin.Execute("CREATE TRIGGER pbc_test_note_failure BEFORE INSERT ON pbc_note "
                      "FOR EACH ROW BEGIN IF NEW.actor_id='" +
                      actor +
                      "' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='Isolated note "
                      "failure'; END IF; END"));
    EXPECT_FALSE(store.Extract(actor, 1, "", {note}, {*source}));
    EXPECT_EQ(store.Observations(actor, true).size(), 1u);
    EXPECT_TRUE(store.Notes(actor).empty());
    ASSERT_TRUE(admin.Execute("DROP TRIGGER pbc_test_note_failure"));
    ASSERT_TRUE(store.Extract(actor, 1, "", {note}, {*source}));
    EXPECT_TRUE(store.Observations(actor, true).empty());
    EXPECT_EQ(store.Notes(actor).size(), 1u);
}

TEST(PBCBudget, CrossLanguageConcurrencyAndUnknownCost)
{
    char const* info = std::getenv("PBC_BUDGET_TEST_INFO");
    char const* budget = std::getenv("PBC_BUDGET_TEST_ID");
    if (!info || !budget)
        GTEST_SKIP() << "Requires the explicitly provisioned isolated budget fixture";

    MySQL::Library_Init();
    struct LibraryGuard
    {
        ~LibraryGuard() { MySQL::Library_End(); }
    } libraryGuard;

    // The Python caller has already charged 100 against this same 1000-nano
    // budget.
    std::vector<std::unique_ptr<PBC::BudgetStore>> stores;
    std::vector<std::future<bool>> pending;
    std::vector<std::string> accepted;
    for (unsigned i = 0; i < 8; ++i)
    {
        auto store = std::make_unique<PBC::BudgetStore>();
        ASSERT_TRUE(store->Open(info, budget));
        stores.push_back(std::move(store));
    }
    for (unsigned i = 0; i < 8; ++i)
    {
        pending.push_back(std::async(std::launch::async,
                                     [&, i]
                                     {
                                         PBC::ApiReservation request;
                                         request.requestId = "cpp-" + std::to_string(i) + "-" + budget;
                                         request.reason = "evaluation";
                                         request.model = "recorded-test";
                                         request.maximumNano = 300;
                                         return stores[i]->Reserve(request);
                                     }));
    }
    for (unsigned i = 0; i < pending.size(); ++i)
        if (pending[i].get())
            accepted.push_back("cpp-" + std::to_string(i) + "-" + budget);
    ASSERT_EQ(accepted.size(), 3u);

    auto& store = *stores[0];
    auto totals = store.Totals();
    ASSERT_TRUE(totals);
    EXPECT_EQ(totals->spentNano, 100u);
    EXPECT_EQ(totals->heldNano, 900u);
    EXPECT_TRUE(store.CancelUnsent(accepted[0]));
    EXPECT_FALSE(store.CancelUnsent(accepted[0]));
    EXPECT_TRUE(store.MarkDispatched(accepted[1]));
    EXPECT_FALSE(store.MarkDispatched(accepted[1]));
    PBC::ApiUsage usage;
    usage.actualNano = 200;
    EXPECT_TRUE(store.Reconcile(accepted[1], usage));
    EXPECT_TRUE(store.Reconcile(accepted[1], usage));
    usage.actualNano = 201;
    EXPECT_FALSE(store.Reconcile(accepted[1], usage));
    for (auto const* outcome : {"stored", "discarded", "malformed", "cancelled"})
        EXPECT_TRUE(store.RecordDelivery(accepted[1], outcome));
    EXPECT_FALSE(store.RecordDelivery(accepted[1], "invented"));
    EXPECT_TRUE(store.MarkDispatched(accepted[2]));
    EXPECT_FALSE(store.CancelUnsent(accepted[2]));
    usage.actualNano.reset();
    usage.providerRequestId = "unknown-charge";
    EXPECT_TRUE(store.Reconcile(accepted[2], usage));

    PBC::BudgetStore restarted;
    ASSERT_TRUE(restarted.Open(info, budget));
    totals = restarted.Totals();
    ASSERT_TRUE(totals);
    EXPECT_EQ(totals->spentNano, 300u);
    EXPECT_EQ(totals->heldNano, 300u);
    PBC::ApiReservation request;
    request.requestId = "cpp-final-" + std::string(budget);
    request.reason = "direct";
    request.model = "recorded-test";
    request.maximumNano = 401;
    EXPECT_FALSE(restarted.Reserve(request));
    request.maximumNano = 400;
    EXPECT_TRUE(restarted.Reserve(request));
    EXPECT_FALSE(restarted.Reserve(request));
}

TEST(PBCBudget, UnbilledLocalRequestIsJournalledWithZeroCeiling)
{
    char const* connectionInfo = std::getenv("PBC_BUDGET_TEST_INFO");
    char const* run = std::getenv("PBC_BUDGET_TEST_ID");
    if (!connectionInfo || !run)
        GTEST_SKIP() << "Requires isolated database fixture";
    MySQL::Library_Init();
    struct Guard
    {
        ~Guard() { MySQL::Library_End(); }
    } guard;
    MySQLConnectionInfo info(connectionInfo);
    FailureFixtureConnection admin(info);
    ASSERT_EQ(admin.Open(), 0u);
    std::string budget = std::string(run) + ":local";
    ASSERT_TRUE(admin.Execute("INSERT INTO pbc_api_budget (budget_id,ceiling_nano) VALUES ('" + budget + "',0)"));
    PBC::BudgetStore ledger;
    ASSERT_TRUE(ledger.Open(connectionInfo, budget));
    PBC::ApiReservation request;
    request.requestId = budget + ":request";
    request.model = "self-hosted-test";
    request.reason = "evaluation";
    EXPECT_FALSE(ledger.Reserve(request));  // A paid/unspecified provider cannot reserve zero.
    request.provider = "self_hosted";
    ASSERT_TRUE(ledger.Reserve(request));
    EXPECT_FALSE(ledger.Reserve(request));
    ASSERT_TRUE(ledger.MarkDispatched(request.requestId));
    PBC::ApiUsage usage;
    usage.actualNano = 0;
    ASSERT_TRUE(ledger.Reconcile(request.requestId, usage));
    auto totals = ledger.Totals();
    ASSERT_TRUE(totals.has_value());
    EXPECT_EQ(totals->spentNano, 0u);
    EXPECT_EQ(totals->heldNano, 0u);
    EXPECT_EQ(totals->ceilingNano, 0u);
}
