// SPDX-License-Identifier: GPL-2.0-or-later
// PBC Character System changes, 2026-09-30; upstream attribution in NOTICE.md.

#include "pbc_dialogue.h"
#include "pbc_context.h"
#include "pbc_json.h"
#include "pbc_model.h"
#include "pbc_interpretation.h"
#include <gtest/gtest.h>
#include <map>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
class Ledger final : public PBC::ApiLedger
{
public:
    bool allow = true;
    bool dispatch = true;
    bool reconcile = true;
    std::vector<PBC::ApiReservation> requests;
    std::map<std::string, PBC::ApiUsage> usage;
    bool Reserve(PBC::ApiReservation const& request) override
    {
        requests.push_back(request);
        return allow;
    }
    bool MarkDispatched(std::string const&) override { return dispatch; }
    bool CancelUnsent(std::string const&) override { return true; }
    bool Reconcile(std::string const& id, PBC::ApiUsage const& value) override
    {
        usage[id] = value;
        return reconcile;
    }
    bool RecordDelivery(std::string const&, std::string const&) override { return true; }
    std::optional<PBC::BudgetTotals> Totals() override { return PBC::BudgetTotals{}; }
};

PBC::ModelSettings Settings()
{
    PBC::ModelSettings settings;
    settings.apiKey = "recorded-test-key";
    settings.selectorInstructions = "Choose the appropriate eligible speaker, or STOP when finished.";
    return settings;
}

PBC::ApiReservation Direct()
{
    PBC::ApiReservation request;
    request.reason = "direct";
    return request;
}
}  // namespace

TEST(PBCModel, DeniedBudgetOrUnconfirmedDispatchNeverSends)
{
    Ledger ledger;
    unsigned calls = 0;
    PBC::ModelGateway gateway(Settings(), ledger,
                              [&](auto const&, auto const&, auto const&, uint32_t)
                              {
                                  ++calls;
                                  return PBC_HttpResponse{};
                              });
    ledger.allow = false;
    EXPECT_FALSE(gateway.Generate("roleplay", "hello", PBC::DialogueSchema(), Direct()).success);
    ledger.allow = true;
    ledger.dispatch = false;
    EXPECT_FALSE(gateway.Generate("roleplay", "hello", PBC::DialogueSchema(), Direct()).success);
    EXPECT_EQ(calls, 0u);
}

TEST(PBCModel, LongEscapedHistoryFitsTheCompleteChatRequest)
{
    Ledger ledger;
    auto settings = Settings();
    settings.contextTokens = 8192;
    PBC::ContextInput input;
    input.actor.id = "anacea";
    input.instructions = "Portray this character.\nRemember what you actually witnessed.";
    input.contextTokens = settings.contextTokens;
    input.outputTokens = settings.outputTokens;
    for (uint64_t id = 1; id <= 512; ++id)
    {
        PBC::ObservationRecord observation;
        observation.source = {id, 1};
        observation.authorId = "orion";
        observation.text = std::string(600, '\\') + std::string(600, '"') + std::string(600, '\n');
        input.observations.push_back(std::move(observation));
    }
    auto context = PBC::BuildCharacterContext(input);
    ASSERT_TRUE(context.success);
    ASSERT_FALSE(context.includedSources.empty());
    EXPECT_EQ(context.includedSources.back().id, 512u);
    EXPECT_LT(context.includedSources.size(), input.observations.size());
    unsigned calls = 0;
    PBC::ModelGateway gateway(settings, ledger,
                              [&](auto const&, auto const& payload, auto const&, uint32_t)
                              {
                                  ++calls;
                                  EXPECT_LE((payload.size() + 3) / 4 + settings.outputTokens, settings.contextTokens);
                                  return PBC_HttpResponse{200, R"({"usage":{"cost":0},"choices":[
            {"finish_reason":"stop","message":{"content":"{}"}}]})"};
                              });
    EXPECT_TRUE(gateway.Generate(context.system, context.user, PBC::DialogueSchema(), Direct()).success);
    EXPECT_EQ(calls, 1u);
}

TEST(PBCModel, SelectorKeepsSeveralThousandTokensOfHistoryAndDistinguishesNewInput)
{
    Ledger ledger;
    unsigned calls = 0;
    PBC::ModelGateway gateway(
        Settings(), ledger,
        [&](auto const&, auto const& payload, auto const&, uint32_t)
        {
            ++calls;
            pbc_json body = pbc_json::parse(payload);
            EXPECT_GT(body["state"]["recent_context"].dump().size(), 8000u);
            EXPECT_EQ(body["state"]["phase"], calls == 1 ? "human_message" : "follow_up");
            pbc_json const& criteria = body["questions"]["speaker"]["criteria"];
            EXPECT_NE(criteria["guard"].get<std::string>().find("Willem"), std::string::npos);
            return PBC_HttpResponse{200,
                                    R"({"usage":{"cost":0},"answers":{"speaker":{"type":"choice","choice":"guard"}}})"};
        });
    PBC::SelectionState state;
    state.remainingTurns = 4;
    state.contribution = "What do you two think?";
    state.candidates = {{"rolock", "Rolock, recent conversational partner", false},
                        {"guard", "Deputy Willem, local questgiver", false}};
    for (unsigned i = 0; i < 80; ++i)
        state.recentContext.push_back(
            {"Rolock: We were discussing Northshire and the work Willem has for "
             "us. "
             "I offered to come along and help after we spoke to him.",
             {"rolock", "guard"}});
    EXPECT_TRUE(gateway.Select(state, Direct()).success);
    state.contribution.clear();
    state.previousSpeaker = "rolock";
    EXPECT_TRUE(gateway.Select(state, Direct()).success);
    EXPECT_EQ(calls, 2u);
}

TEST(PBCModel, TimeoutAndMalformedOutputDoNotRetryOrLoseBilling)
{
    Ledger ledger;
    unsigned calls = 0;
    PBC::ModelGateway gateway(Settings(), ledger,
                              [&](auto const&, auto const&, auto const&, uint32_t)
                              {
                                  ++calls;
                                  if (calls == 1)
                                      return PBC_HttpResponse{};
                                  return PBC_HttpResponse{200, R"({"id":"charged-truncated","usage":{"cost":0.0001},
            "choices":[{"finish_reason":"length","message":{"content":"{\"segments\":["}}]})"};
                              });
    auto first = gateway.Generate("roleplay", "hello", PBC::DialogueSchema(), Direct());
    EXPECT_FALSE(first.success);
    EXPECT_FALSE(first.billingKnown);
    EXPECT_EQ(calls, 1u);
    EXPECT_FALSE(ledger.usage.at(first.requestId).actualNano);
    auto second = gateway.Generate("roleplay", "hello", PBC::DialogueSchema(), Direct());
    EXPECT_FALSE(second.success);
    EXPECT_TRUE(second.billingKnown);
    EXPECT_TRUE(second.text.empty());
    ASSERT_TRUE(ledger.usage.at(second.requestId).actualNano);
    EXPECT_GE(*ledger.usage.at(second.requestId).actualNano, 100000u);
    EXPECT_EQ(calls, 2u);
}

TEST(PBCModel, DecisionsUseTypedEndpointAndValidateEligibleSpeaker)
{
    Ledger ledger;
    unsigned calls = 0;
    PBC::ModelGateway gateway(Settings(), ledger,
                              [&](auto const& url, auto const& payload, auto const&, uint32_t)
                              {
                                  ++calls;
                                  EXPECT_EQ(url, "https://openrouter.ai/api/alpha/decisions");
                                  pbc_json body = pbc_json::parse(payload);
                                  EXPECT_EQ(body["questions"]["speaker"]["type"], "choice");
                                  EXPECT_TRUE(body["questions"]["speaker"]["criteria"].contains("STOP"));
                                  EXPECT_FALSE(body.contains("messages"));
                                  return PBC_HttpResponse{200, R"({"usage":{"cost":0.00001},"answers":{
            "speaker":{"type":"choice","choice":"unwitnessed-stranger"}}})"};
                              });
    PBC::SelectionState state;
    state.remainingTurns = 3;
    state.candidates = {{"anacea", "Present companion", true}};
    EXPECT_EQ(gateway.Select(state, Direct()).text, "anacea");
    EXPECT_EQ(calls, 0u);
    state.candidates.push_back({"guard", "Nearby guard", false});
    EXPECT_EQ(gateway.Select(state, Direct()).text, "anacea");
    EXPECT_EQ(calls, 0u);  // One explicit addressee can answer even with silent
                           // witnesses present.
    state.candidates[0].addressed = false;
    EXPECT_FALSE(gateway.Select(state, Direct()).success);
    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(ledger.requests.back().reason, "selector");
    state.remainingTurns = 0;
    EXPECT_EQ(gateway.Select(state, Direct()).text, "STOP");
    EXPECT_EQ(calls, 1u);
}

TEST(PBCModel, PreviousSpeakerCannotSelectThemselfAgain)
{
    Ledger ledger;
    unsigned calls = 0;
    PBC::ModelGateway gateway(
        Settings(), ledger,
        [&](auto const&, auto const& payload, auto const&, uint32_t)
        {
            ++calls;
            pbc_json body = pbc_json::parse(payload);
            EXPECT_EQ(body["state"]["participants"].size(), 1u);
            EXPECT_EQ(body["state"]["participants"][0]["id"], "anacea");
            EXPECT_FALSE(body["questions"]["speaker"]["criteria"].contains("rolock"));
            return PBC_HttpResponse{
                200, calls == 1 ? R"({"usage":{"cost":0},"answers":{"speaker":{"type":"choice","choice":"rolock"}}})"
                                : R"({"usage":{"cost":0},"answers":{"speaker":{"type":"choice","choice":"anacea"}}})"};
        });
    PBC::SelectionState state;
    state.remainingTurns = 3;
    state.previousSpeaker = "rolock";
    state.candidates = {{"rolock", "Just answered the greeting", false}};
    EXPECT_EQ(gateway.Select(state, Direct()).text, "STOP");
    EXPECT_EQ(calls, 0u);
    EXPECT_TRUE(ledger.requests.empty());
    state.candidates.push_back({"anacea", "Present companion", false});
    EXPECT_FALSE(gateway.Select(state, Direct()).success);  // Reject even a provider that ignores eligibility.
    EXPECT_EQ(gateway.Select(state, Direct()).text, "anacea");
    EXPECT_EQ(calls, 2u);
}

TEST(PBCModel, FullRaidRemainsEligibleAndPrivateCuesStayInSpeakerQuestion)
{
    Ledger ledger;
    PBC::ModelGateway gateway(
        Settings(), ledger,
        [&](auto const&, auto const& payload, auto const&, uint32_t)
        {
            pbc_json body = pbc_json::parse(payload);
            EXPECT_EQ(body["state"]["participants"].size(), 40u);
            EXPECT_EQ(body["questions"]["speaker"]["criteria"].size(), 41u);
            EXPECT_EQ(body["state"].dump().find("lost brother"), std::string::npos);
            EXPECT_NE(body["questions"]["speaker"]["criteria"]["player:40"].get<std::string>().find("lost brother"),
                      std::string::npos);
            return PBC_HttpResponse{
                200, R"({"usage":{"cost":0},"answers":{"speaker":{"type":"choice","choice":"player:40"}}})"};
        });
    PBC::SelectionState state;
    state.remainingTurns = 4;
    state.contribution = "Have any of you been separated from family?";
    for (unsigned i = 1; i <= 40; ++i)
        state.candidates.push_back({"player:" + std::to_string(i), "Raid companion", false,
                                    i == 40 ? "Secret: lost brother" : "Quietly interested in collecting herbs"});
    auto result = gateway.Select(state, Direct());
    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.text, "player:40");
    EXPECT_EQ(ledger.requests.size(), 1u);
}

TEST(PBCModel, SilentOrSingleAddresseeStillInterpretsActionsInOneReservedRequest)
{
    Ledger ledger;
    unsigned calls = 0;
    PBC::ModelGateway gateway(
        Settings(), ledger,
        [&](auto const&, auto const& payload, auto const&, uint32_t)
        {
            ++calls;
            pbc_json body = pbc_json::parse(payload);
            EXPECT_EQ(body["questions"].size(), 2u);
            EXPECT_EQ(body["state"]["actions"]["authorized"], "player:41");
            EXPECT_EQ(body["state"].dump().find("private family history"), std::string::npos);
            return PBC_HttpResponse{
                200, calls < 3 ?
                               R"({"usage":{"cost":0},"answers":{"speaker":{"type":"choice","choice":"STOP"},
                "intent1":{"type":"choice","choice":"stay"}}})"
                               :
                               R"({"usage":{"cost":0},"answers":{"speaker":{"type":"choice","choice":"STOP"},
                "intent1":{"type":"choice","choice":"cheat"}}})"};
        });
    PBC::SelectionState state;
    state.contribution = "Stay here, everyone.";
    state.remainingTurns = 4;
    state.candidates = {{"player:41", "Ithiel", true, "private family history"}};
    state.actionFactsJson = R"({"authorized":"player:41"})";
    state.questions = {{"intent1",
                        "Choose a literal human request; no action for casual conversation.",
                        {{"NONE", "No action"}, {"stay", "Hold this position"}}}};
    auto reply = gateway.Select(state, Direct());
    ASSERT_TRUE(reply.success);
    EXPECT_EQ(reply.text, "STOP");
    EXPECT_EQ(reply.decisions.at("intent1"), "stay");
    EXPECT_EQ(calls, 1u);
    state.remainingTurns = 0;
    EXPECT_TRUE(gateway.Select(state, Direct()).success);
    auto malformed = gateway.Select(state, Direct());
    EXPECT_FALSE(malformed.success);
    EXPECT_TRUE(malformed.decisions.empty());
    EXPECT_EQ(ledger.requests.size(), 3u);
}

TEST(PBCModel, RequiredActionContextCanGrowWithoutDroppingTargetsButKeepsAnAbsoluteCeiling)
{
    Ledger ledger;
    auto settings = Settings();
    settings.selectorTokens = 2000;
    settings.raidSelectorTokens = 8000;
    unsigned calls = 0;
    PBC::ModelGateway gateway(
        settings, ledger,
        [&](auto const&, auto const& payload, auto const&, uint32_t)
        {
            ++calls;
            pbc_json body = pbc_json::parse(payload);
            EXPECT_EQ(body["state"]["actions"]["grounded_offers"].get<std::string>().size(), 12000u);
            EXPECT_EQ(body["questions"]["intent1"]["criteria"].size(), 2u);
            return PBC_HttpResponse{
                200,
                R"({"usage":{"cost":0},"answers":{"speaker":{"type":"choice","choice":"STOP"},"intent1":{"type":"choice","choice":"NONE"}}})"};
        });
    PBC::SelectionState state;
    state.remainingTurns = 1;
    state.contribution = "Buy from the nearby vendor.";
    state.candidates = {{"companion", "My companion", false}};
    state.questions = {{"intent1", "Choose a literal action.", {{"NONE", "No action."}, {"buy", "Purchase."}}}};
    state.actionFactsJson = pbc_json({{"grounded_offers", std::string(12000, 'x')}}).dump();
    EXPECT_TRUE(gateway.Select(state, Direct()).success);
    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(ledger.requests.size(), 1u);
    state.actionFactsJson = pbc_json({{"grounded_offers", std::string(40000, 'x')}}).dump();
    EXPECT_EQ(gateway.Select(state, Direct()).error, "selector_context_limit");
    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(ledger.requests.size(),
              1u);  // Rejection neither sends nor reserves a paid request.
}

TEST(PBCModel, LocalChatUsesStandardSchemaAndExplicitZeroFeeJournal)
{
    Ledger ledger;
    auto settings = Settings();
    settings.chatBackend = "openai-local";
    settings.chatUrl = "http://127.0.0.1:8080/v1/chat/completions";
    settings.apiKey.clear();
    PBC::ModelGateway gateway(
        settings, ledger,
        [&](auto const& url, auto const& payload, auto const& headers, uint32_t)
        {
            EXPECT_EQ(url, settings.chatUrl);
            EXPECT_TRUE(headers.empty());
            auto body = pbc_json::parse(payload);
            EXPECT_FALSE(body.contains("provider"));
            EXPECT_FALSE(body.contains("reasoning"));
            EXPECT_EQ(body["response_format"]["type"], "json_schema");
            pbc_json response = {
                {"usage", {{"prompt_tokens", 20}, {"completion_tokens", 10}}},
                {"choices", pbc_json::array({{{"finish_reason", "stop"}, {"message", {{"content", "{}"}}}}})}};
            return PBC_HttpResponse{200, response.dump()};
        });
    auto result = gateway.Generate("roleplay", "hello", PBC::DialogueSchema(), Direct());
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.billingKnown);
    ASSERT_EQ(ledger.requests.size(), 1u);
    EXPECT_EQ(ledger.requests.back().provider, "self_hosted");
    EXPECT_EQ(ledger.requests.back().maximumNano, 0u);
    EXPECT_EQ(ledger.usage.at(result.requestId).actualNano, 0u);
}

TEST(PBCModel, LocalTypedSelectionDoesNotInheritCloudCredential)
{
    Ledger ledger;
    auto settings = Settings();
    settings.selectorBackend = "systemone-local";
    settings.decisionUrl = "http://127.0.0.1:8000/v1/systemone";
    settings.selectorModel = "typed-decisions";
    PBC::ModelGateway gateway(settings, ledger,
                              [&](auto const& url, auto const& payload, auto const& headers, uint32_t)
                              {
                                  EXPECT_EQ(url, settings.decisionUrl);
                                  EXPECT_TRUE(headers.empty());
                                  auto body = pbc_json::parse(payload);
                                  EXPECT_EQ(body["model"], "typed-decisions");
                                  EXPECT_TRUE(body["questions"]["speaker"]["criteria"].contains("guard"));
                                  return PBC_HttpResponse{200, R"({"usage":{"input_tokens":30,"output_tokens":0},
            "answers":{"speaker":{"type":"choice","choice":"guard"}}})"};
                              });
    PBC::SelectionState state;
    state.remainingTurns = 2;
    state.candidates = {{"guard", "Local guard", false}, {"friend", "Companion", false}};
    auto result = gateway.Select(state, Direct());
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.billingKnown);
    EXPECT_EQ(result.text, "guard");
    EXPECT_EQ(ledger.requests.back().maximumNano, 0u);
}

TEST(PBCModel, MissingPaidCostStillKeepsUnknownHoldAndLocalFailureIsUnbilled)
{
    Ledger ledger;
    auto settings = Settings();
    PBC::ModelGateway paid(settings, ledger, [](auto const&, auto const&, auto const&, uint32_t)
                           { return PBC_HttpResponse{503, R"({"error":{"message":"unavailable"}})"}; });
    auto result = paid.Generate("roleplay", "hello", PBC::DialogueSchema(), Direct());
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.billingKnown);
    EXPECT_FALSE(ledger.usage.at(result.requestId).actualNano.has_value());
    EXPECT_GT(ledger.requests.back().maximumNano, 0u);
    settings.chatBackend = "openai-local";
    settings.chatUrl = "http://127.0.0.1:8080/v1/chat/completions";
    settings.apiKey.clear();
    PBC::ModelGateway local(settings, ledger,
                            [](auto const&, auto const&, auto const&, uint32_t) { return PBC_HttpResponse{}; });
    result = local.Generate("roleplay", "hello", PBC::DialogueSchema(), Direct());
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.billingKnown);
    EXPECT_EQ(ledger.usage.at(result.requestId).actualNano, 0u);
    unsigned calls = 0;
    settings.chatUrl = "https://openrouter.ai/api/v1/chat/completions";
    PBC::ModelGateway invalid(settings, ledger,
                              [&](auto const&, auto const&, auto const&, uint32_t)
                              {
                                  ++calls;
                                  return PBC_HttpResponse{};
                              });
    EXPECT_EQ(invalid.Generate("roleplay", "hello", PBC::DialogueSchema(), Direct()).error, "invalid_local_endpoint");
    settings.chatUrl = "https://OPENROUTER.AI/api/v1/chat/completions";
    PBC::ModelGateway uppercase(settings, ledger,
                                [&](auto const&, auto const&, auto const&, uint32_t)
                                {
                                    ++calls;
                                    return PBC_HttpResponse{};
                                });
    EXPECT_EQ(uppercase.Generate("roleplay", "hello", PBC::DialogueSchema(), Direct()).error, "invalid_local_endpoint");
    EXPECT_EQ(calls, 0u);
}

namespace
{
struct CaptureDirectory
{
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("pbc-capture-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~CaptureDirectory() { std::filesystem::remove_all(path); }
};

PBC::SelectionState CaptureState()
{
    PBC::SelectionState state;
    state.remainingTurns = 4;
    state.contribution = "Please remain beside this tree.";
    state.candidates = {{"companion", "My present companion", true}};
    state.questions = {{"intent1",
                        "Identify the requested operation.",
                        {{"stay", "Hold position."}, {"NONE", "No requested operation."}}}};
    return state;
}

std::string CaptureResponse()
{
    return R"({"usage":{"cost":0},"answers":{
        "speaker":{"type":"choice","choice":"companion","probabilities":{"companion":0.81,"STOP":0.19},"confidence":0.62},
        "intent1":{"type":"choice","choice":"stay","probabilities":{"stay":0.93,"NONE":0.07},"confidence":0.86}}})";
}

pbc_json ReadCapture(std::filesystem::path const& path)
{
    std::ifstream input(path, std::ios::binary);
    return pbc_json::parse(input);
}
}  // namespace

TEST(PBCModel, FullNativeActionQuestionsIncludeReferenceFieldsAndRejectFollowupActions)
{
    Ledger ledger;
    unsigned calls = 0;
    auto state = CaptureState();
    state.actionFactsJson = "{}";
    state.questions.clear();
    for (unsigned i = 0; i < 24; ++i)
        state.questions.push_back(
            {"field" + std::to_string(i), "Interpret native request field.", {{"NONE", "Unused."}}});
    PBC::ModelGateway gateway(Settings(), ledger,
                              [&](auto const&, auto const& payload, auto const&, uint32_t)
                              {
                                  ++calls;
                                  auto body = pbc_json::parse(payload);
                                  EXPECT_EQ(body["questions"].size(), 25u);
                                  pbc_json answers = {{"speaker", {{"type", "choice"}, {"choice", "companion"}}}};
                                  for (auto const& question : state.questions)
                                      answers[question.id] = {{"type", "choice"}, {"choice", "NONE"}};
                                  return PBC_HttpResponse{
                                      200, pbc_json({{"usage", {{"cost", 0}}}, {"answers", answers}}).dump()};
                              });
    EXPECT_TRUE(gateway.Select(state, Direct()).success);
    state.contribution.clear();
    state.previousSpeaker = "companion";
    EXPECT_FALSE(gateway.Select(state, Direct()).success);
    EXPECT_EQ(calls, 1u);
}

TEST(PBCModel, CapturesActualRequestAndFullEvidenceWithoutChangingSelection)
{
    CaptureDirectory directory;
    Ledger ledger;
    auto settings = Settings();
    settings.selectorCapturePath = directory.path.string();
    std::string actual;
    PBC::ModelGateway gateway(settings, ledger,
                              [&](auto const&, auto const& body, auto const&, uint32_t)
                              {
                                  actual = body;
                                  return PBC_HttpResponse{200, CaptureResponse()};
                              });
    auto reply = gateway.Select(CaptureState(), Direct());
    ASSERT_TRUE(reply.success);
    EXPECT_EQ(reply.text, "companion");
    EXPECT_EQ(reply.decisions.at("intent1"), "stay");
    auto evidence = pbc_json::parse(reply.decisionAnswersJson);
    EXPECT_EQ(evidence["speaker"]["probabilities"]["STOP"], 0.19);
    EXPECT_EQ(evidence["intent1"]["confidence"], 0.86);
    auto record = ReadCapture(directory.path / ("pbc-decision-" + reply.requestId + ".json"));
    EXPECT_EQ(record["request"], pbc_json::parse(actual));
    EXPECT_EQ(record["response"]["answers"], evidence);
    EXPECT_EQ(record["reserved_nano"], ledger.requests.back().maximumNano);
    EXPECT_FALSE(record.contains("headers"));
    auto permission = std::filesystem::status(directory.path).permissions();
    EXPECT_EQ(permission & (std::filesystem::perms::group_all | std::filesystem::perms::others_all),
              std::filesystem::perms::none);
}

TEST(PBCModel, CaptureBoundsPersistAcrossGatewaysAndNeverAffectAccounting)
{
    CaptureDirectory directory;
    Ledger ledger;
    auto settings = Settings();
    settings.selectorCapturePath = directory.path.string();
    settings.selectorCaptureMaxRecords = 1;
    auto transport = [](auto const&, auto const&, auto const&, uint32_t)
    { return PBC_HttpResponse{200, CaptureResponse()}; };
    PBC::ModelGateway first(settings, ledger, transport);
    EXPECT_TRUE(first.Select(CaptureState(), Direct()).success);
    PBC::ModelGateway restarted(settings, ledger, transport);
    EXPECT_TRUE(restarted.Select(CaptureState(), Direct()).success);
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory.path), std::filesystem::directory_iterator{}),
              1);
    EXPECT_EQ(ledger.usage.size(), 2u);
    settings.selectorCaptureMaxBytes = 1;
    settings.selectorCaptureMaxRecords = 256;
    PBC::ModelGateway byteBound(settings, ledger, transport);
    EXPECT_TRUE(byteBound.Select(CaptureState(), Direct()).success);
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory.path), std::filesystem::directory_iterator{}),
              1);
    EXPECT_EQ(ledger.usage.size(), 3u);
}

TEST(PBCModel, CaptureRedactsEchoedCredentialsAndPreservesUnknownCostFailures)
{
    CaptureDirectory directory;
    Ledger ledger;
    auto settings = Settings();
    settings.apiKey = "credential-with-\"quote\n";
    settings.selectorApiKey = "separate-selector-secret";
    settings.selectorCapturePath = directory.path.string();
    auto state = CaptureState();
    state.contribution += settings.apiKey + settings.selectorApiKey;
    PBC::ModelGateway gateway(
        settings, ledger,
        [&](auto const&, auto const&, auto const&, uint32_t)
        {
            return PBC_HttpResponse{
                400, pbc_json({{"error", {{"message", settings.apiKey + settings.selectorApiKey}}}}).dump()};
        });
    auto reply = gateway.Select(state, Direct());
    EXPECT_FALSE(reply.success);
    EXPECT_FALSE(reply.billingKnown);
    ASSERT_TRUE(ledger.usage.contains(reply.requestId));
    EXPECT_FALSE(ledger.usage.at(reply.requestId).actualNano);
    auto record = ReadCapture(directory.path / ("pbc-decision-" + reply.requestId + ".json"));
    auto text = record.dump();
    EXPECT_EQ(text.find(settings.selectorApiKey), std::string::npos);
    EXPECT_EQ(record["request"]["state"]["contribution"].get<std::string>().find(settings.apiKey), std::string::npos);
    EXPECT_EQ(record["response"]["error"]["message"], "[credential redacted][credential redacted]");
}

TEST(PBCModel, CaptureDisabledDeniedOrUnwritableNeverChangesTransportDecisions)
{
    CaptureDirectory directory;
    Ledger ledger;
    auto settings = Settings();
    unsigned calls = 0;
    auto transport = [&](auto const&, auto const&, auto const&, uint32_t)
    {
        ++calls;
        return PBC_HttpResponse{200, CaptureResponse()};
    };
    PBC::ModelGateway disabled(settings, ledger, transport);
    EXPECT_TRUE(disabled.Select(CaptureState(), Direct()).success);
    EXPECT_FALSE(std::filesystem::exists(directory.path));
    settings.selectorCapturePath = directory.path.string();
    ledger.allow = false;
    PBC::ModelGateway denied(settings, ledger, transport);
    EXPECT_FALSE(denied.Select(CaptureState(), Direct()).success);
    EXPECT_FALSE(std::filesystem::exists(directory.path));
    ledger.allow = true;
    // A regular file cannot be a capture directory. No network retry or dropped action.
    std::ofstream blocking(directory.path, std::ios::binary);
    blocking.close();
    PBC::ModelGateway unwritable(settings, ledger, transport);
    EXPECT_TRUE(unwritable.Select(CaptureState(), Direct()).success);
    EXPECT_EQ(calls, 2u);
    EXPECT_EQ(ledger.usage.size(), 2u);
}

TEST(PBCModel, OptionalReadingKeepsNativeStateAndDoesNotSharePrivateSpeakerCues)
{
    Ledger ledger;
    unsigned calls = 0;
    PBC::ModelGateway gateway(Settings(), ledger,
                              [&](auto const&, auto const& payload, auto const&, uint32_t)
                              {
                                  auto body = pbc_json::parse(payload);
                                  ++calls;
                                  if (calls == 1)
                                  {
                                      EXPECT_NE(payload.find("speaker-only-secret"), std::string::npos);
                                      return PBC_HttpResponse{200, R"({"usage":{"cost":0},"answers":{
                "speaker":{"type":"choice","choice":"mira"},
                "intent1":{"type":"choice","choice":"follow","probabilities":{"follow":1},"confidence":1},
                "recipient1":{"type":"choice","choice":"ALL","probabilities":{"ALL":0.9,"mira":0.1},"confidence":0.9},
                "second_action":{"type":"choice","choice":"NONE","probabilities":{"NONE":1},"confidence":1}}})"};
                                  }
                                  EXPECT_EQ(calls, 2u);
                                  EXPECT_EQ(body["questions"].size(), 1u);
                                  EXPECT_EQ(payload.find("speaker-only-secret"), std::string::npos);
                                  EXPECT_TRUE(body["state"]["actions"]["controlled_companions"][1]["holding_position"]);
                                  for (auto const& [id, description] : body["questions"]["reading"]["criteria"].items())
                                      if (auto text = description.template get<std::string>();
                                          text.find("\"recipient\":\"Mira\"") != std::string::npos &&
                                          text.find("\"meaning\":\"Follow\"") != std::string::npos)
                                          return PBC_HttpResponse{
                                              200,
                                              pbc_json{{"usage", {{"cost", 0}}},
                                                       {"answers", {{"reading", {{"type", "choice"}, {"choice", id}}}}}}
                                                  .dump()};
                                  ADD_FAILURE() << "Complete reading omitted the explicit actor";
                                  return PBC_HttpResponse{};
                              });
    PBC::SelectionState state;
    state.contribution = "Mira accompany me; Bram continue your watch.";
    state.remainingTurns = 2;
    state.candidates = {{"mira", "Mira", false, "speaker-only-secret"}, {"bram", "Bram", false}};
    state.actionFactsJson =
        R"({"controlled_companions":[{"id":"mira","label":"Mira","holding_position":true},{"id":"bram","label":"Bram","holding_position":true}]})";
    state.questions = {{"intent1", "Meaning", {{"follow", "Follow"}, {"NONE", "None"}}},
                       {"recipient1", "Recipients", {{"ALL", "All"}, {"mira", "Mira"}, {"bram", "Bram"}}},
                       {"second_action", "Sequence", {{"NONE", "One"}}}};
    auto reply = gateway.Select(state, Direct());
    ASSERT_TRUE(reply.success) << reply.error;
    EXPECT_EQ(reply.decisions.at("recipient1"), "mira");
    EXPECT_EQ(reply.text, "mira");
    EXPECT_EQ(calls, 2u);
    EXPECT_EQ(ledger.requests.size(), 2u);
    EXPECT_EQ(ledger.usage.size(), 2u);
    EXPECT_NE(reply.decisionAnswersJson.find("probabilities"), std::string::npos);
}

TEST(PBCModel, MissingReadingAndBadProbabilityOptionsNeverAuthorizeSubstitutes)
{
    pbc_json request = {
        {"state",
         {{"contribution", "The pair please"},
          {"actions", {{"controlled_companions", pbc_json::array({{{"id", "mira"}, {"label", "Mira"}}})}}}}},
        {"questions",
         {{"intent1", {{"criteria", {{"trade", "Transfer"}}}}},
          {"recipient1", {{"criteria", {{"mira", "Mira"}}}}},
          {"item1", {{"criteria", {{"item:117", "Jerky"}, {"item:159", "Water"}, {"UNAVAILABLE", "Missing"}}}}},
          {"quantity1", {{"criteria", {{"PAIR", "Pair"}}}}}}}};
    pbc_json answers = {
        {"intent1", {{"choice", "trade"}}},
        {"recipient1", {{"choice", "mira"}}},
        {"item1",
         {{"choice", "item:117"}, {"probabilities", {{"item:117", 0.6}, {"item:159", 0.4}, {"item:999", 1.0}}}}},
        {"quantity1", {{"choice", "PAIR"}}},
        {"second_action", {{"choice", "NONE"}}}};
    EXPECT_FALSE(PBC::NeedsCompleteReading(request, answers));
    auto readings = PBC::CompleteReadings(request, answers);
    ASSERT_TRUE(readings.contains("OTHER"));
    EXPECT_TRUE(readings.at("OTHER").choices.empty());
    EXPECT_TRUE(std::any_of(
        readings.begin(), readings.end(), [](auto const& entry)
        { return entry.second.items == std::map<std::string, uint32_t>{{"item:117", 1}, {"item:159", 1}}; }));
    for (auto const& [id, reading] : readings)
        EXPECT_EQ(reading.description.find("item:999"), std::string::npos);
}

TEST(PBCModel, IndependentSequenceAndTargetAnswersRetainClarificationAlternatives)
{
    pbc_json request = {
        {"state",
         {{"contribution", "Guard this spot briefly, then rejoin us."},
          {"actions", {{"controlled_companions", pbc_json::array({{{"id", "mira"}, {"label", "Mira"}}})}}}}},
        {"questions",
         {{"intent1", {{"criteria", {{"stay", "Hold"}, {"attack", "Attack"}}}}},
          {"intent2", {{"criteria", {{"regroup", "Return"}, {"NONE", "None"}}}}},
          {"recipient1", {{"criteria", {{"mira", "Mira"}}}}},
          {"second_action", {{"criteria", {{"YES", "Two operations"}, {"NONE", "One operation"}}}}},
          {"target1", {{"criteria", {{"boar", "A boar"}, {"wolf", "A wolf"}, {"CLARIFY", "Unclear"}}}}}}}};
    pbc_json answers = {{"intent1", {{"choice", "stay"}, {"confidence", 1}, {"probabilities", {{"stay", 1}}}}},
                        {"recipient1", {{"choice", "mira"}, {"confidence", 1}}},
                        {"intent2", {{"choice", "regroup"}, {"probabilities", {{"regroup", 0.7}, {"NONE", 0.3}}}}},
                        {"second_action", {{"choice", "NONE"}}}};
    EXPECT_TRUE(PBC::NeedsCompleteReading(request, answers));
    auto readings = PBC::CompleteReadings(request, answers);
    EXPECT_TRUE(std::any_of(readings.begin(), readings.end(),
                            [](auto const& entry)
                            {
                                return entry.second.choices.contains("second_action") &&
                                       entry.second.choices.at("second_action") == "YES" &&
                                       entry.second.choices.at("intent2") == "regroup";
                            }));
    answers["intent1"] = {{"choice", "attack"}, {"confidence", 1}, {"probabilities", {{"attack", 1}}}};
    answers["intent2"] = {{"choice", "NONE"}};
    answers["target1"] = {{"choice", "boar"}, {"confidence", 0.7}, {"probabilities", {{"boar", 0.7}, {"wolf", 0.3}}}};
    EXPECT_TRUE(PBC::NeedsCompleteReading(request, answers));
    readings = PBC::CompleteReadings(request, answers);
    EXPECT_TRUE(std::any_of(readings.begin(), readings.end(),
                            [](auto const& entry)
                            {
                                return entry.second.choices.contains("target1") &&
                                       entry.second.choices.at("target1") == "CLARIFY" &&
                                       entry.second.choices.at("intent1") == "attack";
                            }));
    request["state"]["actions"]["targets"] = pbc_json::array({{{"selected_at_input", true}}});
    EXPECT_TRUE(PBC::NeedsCompleteReading(request, answers));
}

TEST(PBCModel, CompoundReadingsCanCorrectFirstClauseWithoutChangingSecondActorOrTarget)
{
    auto request = pbc_json::parse(
        R"({"state":{"contribution":"Arrange separate waiting places while our tank brings in the boar.",
        "actions":{"controlled_companions":[{"id":"tank","label":"Rurik"},{"id":"healer","label":"Mira"}]}},
        "questions":{
          "intent1":{"criteria":{"pull":"Bring in enemy","formation":"Change spatial layout","stay":"Wait without changing layout"}},
          "intent2":{"criteria":{"pull":"Bring in enemy","NONE":"Nothing"}},
          "recipient1":{"criteria":{"ALL":"Everyone","tank":"Tank"}},
          "recipient2":{"criteria":{"tank":"Tank","ALL":"Everyone"}},
          "formation1":{"criteria":{"spread_hold":"Separate positions, held until return"}},
          "formation2":{"criteria":{"NONE":"No layout change"}},
          "target1":{"criteria":{"boar":"Selected boar","NONE":"No target"}},
          "target2":{"criteria":{"boar":"Selected boar","NONE":"No target"}},
          "second_action":{"criteria":{"YES":"Two new operations","NONE":"One operation"}}}})");
    auto answers = pbc_json::parse(R"({
        "intent1":{"choice":"pull","confidence":0.7,"probabilities":{"pull":0.7,"formation":0.2,"stay":0.1}},
        "intent2":{"choice":"pull","probabilities":{"pull":0.9,"NONE":0.1}},
        "recipient1":{"choice":"ALL","probabilities":{"ALL":0.9,"tank":0.1}},
        "recipient2":{"choice":"tank","probabilities":{"tank":0.9,"ALL":0.1}},
        "formation1":{"choice":"spread_hold"},"formation2":{"choice":"NONE"},
        "target1":{"choice":"boar"},"target2":{"choice":"boar"},"second_action":{"choice":"YES"}})");
    EXPECT_TRUE(PBC::NeedsCompleteReading(request, answers));
    auto readings = PBC::CompleteReadings(request, answers);
    EXPECT_TRUE(std::any_of(readings.begin(), readings.end(),
                            [](auto const& entry)
                            {
                                auto const& choices = entry.second.choices;
                                return choices.contains("intent2") && choices.at("intent1") == "formation" &&
                                       choices.at("recipient1") == "ALL" && choices.at("formation1") == "spread_hold" &&
                                       choices.at("intent2") == "pull" && choices.at("recipient2") == "tank" &&
                                       choices.at("target2") == "boar" && choices.at("second_action") == "YES";
                            }));
    EXPECT_TRUE(readings.contains("OTHER"));
    EXPECT_LE(readings.size(), 97u);
}

TEST(PBCModel, UncertainEquipmentDirectionAndPerformanceNegationNeedCompleteMeaning)
{
    auto request = pbc_json::parse(R"({"state":{"actions":{"controlled_companions":[{"id":"mira"}]}},
        "questions":{"intent1":{"criteria":{"equip":"Put on owned item","unequip":"Put worn item away",
        "stop_dance":"Stop current dance","NONE":"No new operation"}},
        "item1":{"criteria":{"equipped:head":"Worn head slot","CLARIFY":"Ambiguous gear"}},
        "recipient1":{"criteria":{"mira":"Mira"}}}})");
    auto answers = pbc_json::parse(R"({"intent1":{"choice":"equip","confidence":0.55,
        "probabilities":{"equip":0.55,"unequip":0.4,"NONE":0.05}},
        "recipient1":{"choice":"mira","confidence":1},"item1":{"choice":"equipped:head"},
        "second_action":{"choice":"NONE"}})");
    EXPECT_TRUE(PBC::NeedsCompleteReading(request, answers));
    auto readings = PBC::CompleteReadings(request, answers);
    EXPECT_TRUE(
        std::any_of(readings.begin(), readings.end(), [](auto const& reading)
                    { return !reading.second.choices.empty() && reading.second.choices.at("intent1") == "unequip"; }));
    answers["intent1"] = {
        {"choice", "stop_dance"}, {"confidence", 0.45}, {"probabilities", {{"stop_dance", 0.5}, {"NONE", 0.5}}}};
    EXPECT_TRUE(PBC::NeedsCompleteReading(request, answers));
    readings = PBC::CompleteReadings(request, answers);
    EXPECT_TRUE(
        std::any_of(readings.begin(), readings.end(), [](auto const& reading)
                    { return !reading.second.choices.empty() && reading.second.choices.at("intent1") == "NONE"; }));
    answers["intent1"]["confidence"] = 0.95;
    EXPECT_TRUE(PBC::NeedsCompleteReading(request, answers));
    request["state"]["actions"]["controlled_companions"][0]["dancing"] = false;
    EXPECT_TRUE(PBC::NeedsCompleteReading(request, answers));
    request["state"]["actions"]["controlled_companions"][0]["dancing"] = true;
    EXPECT_TRUE(PBC::NeedsCompleteReading(request, answers));
    answers["intent1"] = {{"choice", "equip"}, {"confidence", 0.99}, {"probabilities", {{"equip", 1.0}}}};
    answers["item1"] = {{"choice", "UNAVAILABLE"}, {"confidence", 0.55}};
    EXPECT_TRUE(PBC::NeedsCompleteReading(request, answers));
    answers["item1"]["confidence"] = 0.95;
    EXPECT_TRUE(PBC::NeedsCompleteReading(request, answers));
}

TEST(PBCModel, CompoundEquipmentPerformanceRetainsSlotAndUnavailableMeanings)
{
    auto request = pbc_json::parse(R"({"state":{"actions":{"controlled_companions":[{"id":"mira"}]}},
      "questions":{"intent1":{"criteria":{"unequip":"Remove worn gear","dance":"Start a dance"}},
        "intent2":{"criteria":{"dance":"Start a dance"}},
        "recipient1":{"criteria":{"mira":"Mira"}},"recipient2":{"criteria":{"mira":"Mira"}},
        "item1":{"criteria":{"CLARIFY":"Unspecified gear","equipped:chest":"Worn chest slot",
                               "UNAVAILABLE":"Specific unavailable gear"}},
        "second_action":{"criteria":{"YES":"Two operations","NONE":"One operation"}}}})");
    auto answers = pbc_json::parse(R"({
      "intent1":{"choice":"unequip","probabilities":{"unequip":0.9,"dance":0.1}},
      "intent2":{"choice":"dance","probabilities":{"dance":1}},
      "recipient1":{"choice":"mira"},"recipient2":{"choice":"mira"},
      "item1":{"choice":"CLARIFY","probabilities":{"CLARIFY":0.6,"equipped:chest":0.3,"UNAVAILABLE":0.1}},
      "item2":{"choice":"NONE"},"second_action":{"choice":"YES"}})");
    auto readings = PBC::CompleteReadings(request, answers);
    for (auto const& item : {"equipped:chest", "CLARIFY", "UNAVAILABLE"})
        EXPECT_TRUE(std::any_of(readings.begin(), readings.end(),
                                [&](auto const& reading)
                                {
                                    auto const& choices = reading.second.choices;
                                    return !choices.empty() && choices.at("intent1") == "unequip" &&
                                           choices.at("item1") == item && choices.at("intent2") == "dance" &&
                                           choices.at("second_action") == "YES";
                                }));
}

TEST(PBCModel, ConfidentGameplayRequiresCompleteMeaningWhileConversationMayBypass)
{
    auto request = pbc_json::parse(R"({"state":{"actions":{"controlled_companions":[{"id":"mira"}]}},
      "questions":{"intent1":{"criteria":{"stay":"Hold"}},"intent2":{"criteria":{"trade":"Transfer"}},
      "recipient1":{"criteria":{"mira":"Mira"}},"recipient2":{"criteria":{"mira":"Mira"}},
      "item2":{"criteria":{"item:117":"Jerky","UNAVAILABLE":"Specific missing item"}},
      "second_action":{"criteria":{"YES":"Two operations","NONE":"One operation"}}}})");
    auto answers = pbc_json::parse(R"({"intent1":{"choice":"stay","confidence":0.99,"probabilities":{"stay":1}},
      "intent2":{"choice":"trade","confidence":0.99},"second_action":{"choice":"YES","confidence":0.99},
      "recipient1":{"choice":"mira","confidence":0.99},"recipient2":{"choice":"mira","confidence":0.99},
      "item1":{"choice":"UNAVAILABLE","confidence":0.1},"item2":{"choice":"item:117","confidence":0.6},
      "quantity2":{"choice":"NUM:1","confidence":0.99}})");
    EXPECT_TRUE(PBC::NeedsCompleteReading(request, answers));
    answers["item2"]["confidence"] = 0.99;
    EXPECT_TRUE(PBC::NeedsCompleteReading(request, answers));
    answers["intent1"] = {{"choice", "NONE"}, {"confidence", 0.99}, {"probabilities", {{"NONE", 1.0}}}};
    answers["intent2"]["choice"] = answers["second_action"]["choice"] = "NONE";
    EXPECT_FALSE(PBC::NeedsCompleteReading(request, answers));
}

TEST(PBCModel, CompleteMeaningsPreserveOrderAndDistinguishContinuingAnExistingHold)
{
    auto request = pbc_json::parse(R"({"state":{"actions":{"controlled_companions":[
      {"id":"mira","label":"Mira","holding_position":true}]}},"questions":{
      "intent1":{"criteria":{"follow":"Follow","stay":"Hold","keep_order":"Continue"}},
      "intent2":{"criteria":{"stay":"Hold","dance":"Dance"}},
      "recipient1":{"criteria":{"mira":"Mira"}},"recipient2":{"criteria":{"mira":"Mira"}},
      "duration1":{"criteria":{"NONE":"Indefinite","FIVE_MINUTES":"Five minutes"}},
      "duration2":{"criteria":{"NONE":"Indefinite","FIVE_MINUTES":"Five minutes"}},
      "second_action":{"criteria":{"YES":"Two operations","NONE":"One operation"}}}})");
    auto answers = pbc_json::parse(R"({"intent1":{"choice":"follow"},"intent2":{"choice":"stay"},
      "recipient1":{"choice":"mira"},"recipient2":{"choice":"mira"},
      "duration1":{"choice":"NONE"},"duration2":{"choice":"FIVE_MINUTES"},
      "second_action":{"choice":"YES"}})");
    auto readings = PBC::CompleteReadings(request, answers);
    EXPECT_TRUE(std::any_of(readings.begin(), readings.end(),
                            [](auto const& entry)
                            {
                                auto const& c = entry.second.choices;
                                return !c.empty() && c.at("intent1") == "follow" && c.at("intent2") == "stay" &&
                                       c.at("second_action") == "YES" && c.at("duration2") == "FIVE_MINUTES";
                            }));
    // The actor was holding before the message. That does not make a hold AFTER
    // following redundant, nor prove whether an untimed first hold is a renewal.
    answers["intent1"]["choice"] = "stay";
    answers["intent2"]["choice"] = "dance";
    readings = PBC::CompleteReadings(request, answers);
    bool renewThenDance = false, continueThenDance = false;
    for (auto const& [id, reading] : readings)
    {
        auto const& c = reading.choices;
        if (c.empty())
            continue;
        renewThenDance |= c.at("intent1") == "stay" && c.at("intent2") == "dance" && c.at("duration1") == "NONE" &&
                          c.at("duration2") == "FIVE_MINUTES";
        continueThenDance |= c.at("intent1") == "dance" && c.at("second_action") == "NONE" &&
                             c.at("duration1") == "FIVE_MINUTES" && c.at("recipient1") == "mira" &&
                             reading.description.find("existing hold and its timer unchanged") != std::string::npos;
    }
    EXPECT_TRUE(renewThenDance);
    EXPECT_TRUE(continueThenDance);
}

TEST(PBCModel, CompleteMeaningDescriptionsAgreeWithLayoutsAndExactMixedBundles)
{
    auto request = pbc_json::parse(R"({"state":{"actions":{"controlled_companions":[{"id":"mira","label":"Mira"}]}},
      "questions":{"intent1":{"criteria":{"formation":"Arrange","trade":"Transfer"}},
      "intent2":{"criteria":{"formation":"Arrange"}},
      "recipient1":{"criteria":{"mira":"Mira"}},"recipient2":{"criteria":{"mira":"Mira"}},
      "formation1":{"criteria":{"spread":"Spaced following","spread_hold":"Spaced waiting"}},
      "formation2":{"criteria":{"spread":"Spaced following","spread_hold":"Spaced waiting"}},
      "item1":{"criteria":{"item:117":"Jerky","item:159":"Water","item:999":"Apple"}},
      "quantity1":{"criteria":{"PAIR":"Two units"}},
      "second_action":{"criteria":{"YES":"Two operations","NONE":"One operation"}}}})");
    auto answers = pbc_json::parse(R"({"intent1":{"choice":"formation"},"intent2":{"choice":"formation"},
      "recipient1":{"choice":"mira"},"recipient2":{"choice":"mira"},
      "formation1":{"choice":"spread","probabilities":{"spread":0.7,"spread_hold":0.3}},
      "formation2":{"choice":"spread","probabilities":{"spread":0.7,"spread_hold":0.3}},
      "item1":{"choice":"item:999","probabilities":{"item:117":0.5,"item:159":0.3,"item:999":0.2}},
      "quantity1":{"choice":"PAIR"},"second_action":{"choice":"YES"}})");
    bool firstWaiting = false, secondWaiting = false, singleWaiting = false;
    for (auto const& [id, reading] : PBC::CompleteReadings(request, answers))
    {
        if (reading.choices.empty() || reading.choices.at("intent1") == "NONE")
            continue;
        auto start = reading.description.find('[');
        auto end = reading.description.rfind(']');
        ASSERT_NE(start, std::string::npos);
        auto steps = pbc_json::parse(reading.description.substr(start, end - start + 1));
        for (std::size_t i = 0; i < steps.size(); ++i)
        {
            auto key = "formation" + std::to_string(i + 1);
            EXPECT_EQ(steps[i]["formation"],
                      reading.choices.at(key) + ": " +
                          request["questions"][key]["criteria"][reading.choices.at(key)].get<std::string>());
        }
        firstWaiting |= steps.size() == 2 && reading.choices.at("formation1") == "spread_hold";
        secondWaiting |= steps.size() == 2 && reading.choices.at("formation2") == "spread_hold";
        singleWaiting |= steps.size() == 1 && reading.choices.at("formation1") == "spread_hold";
    }
    EXPECT_TRUE(firstWaiting);
    EXPECT_TRUE(secondWaiting);
    EXPECT_TRUE(singleWaiting);
    answers["intent1"]["choice"] = "trade";
    bool mixed = false;
    for (auto const& [id, reading] : PBC::CompleteReadings(request, answers))
        if (reading.items == std::map<std::string, uint32_t>{{"item:117", 1}, {"item:159", 1}})
        {
            mixed = true;
            EXPECT_NE(reading.description.find("Jerky"), std::string::npos);
            EXPECT_NE(reading.description.find("Water"), std::string::npos);
            EXPECT_EQ(reading.description.find("Apple"), std::string::npos);
        }
    EXPECT_TRUE(mixed);
}

TEST(PBCModel, JointAmbiguityRejectsDifferentTargetsButPreservesEquivalentRecipientsAndConversation)
{
    auto state = pbc_json::parse(R"({"controlled_companions":[{"id":"mira"}]})");
    std::map<std::string, PBC::InterpretationReading> readings = {
        {"wolf",
         {{{"intent1", "attack"}, {"recipient1", "mira"}, {"target1", "wolf"}, {"second_action", "NONE"}}, {}, ""}},
        {"boar",
         {{{"intent1", "attack"}, {"recipient1", "mira"}, {"target1", "boar"}, {"second_action", "NONE"}}, {}, ""}},
        {"conversation", {{{"intent1", "NONE"}, {"recipient1", "mira"}, {"second_action", "NONE"}}, {}, ""}},
        {"OTHER", {{}, {}, ""}}};
    auto answer = pbc_json::parse(R"({"choice":"wolf","probabilities":{"wolf":0.49,"boar":0.48,"OTHER":0.03}})");
    EXPECT_TRUE(PBC::AmbiguousCompleteReading(readings, answer, state));
    readings["boar"].choices["target1"] = "wolf";
    readings["boar"].choices["recipient1"] = "ALL";
    EXPECT_FALSE(PBC::AmbiguousCompleteReading(readings, answer, state));
    readings["wolf"].choices["reference1"] = "IDENTIFIED";
    readings["boar"].choices["reference1"] = "SELECTED";
    EXPECT_FALSE(PBC::AmbiguousCompleteReading(readings, answer, state));
    answer = pbc_json::parse(R"({"choice":"conversation","probabilities":{"conversation":0.52,"wolf":0.48}})");
    EXPECT_FALSE(PBC::AmbiguousCompleteReading(readings, answer, state));
}

TEST(PBCModel, DecliningSupplyRetainsItsExactOfferAndLaterMovementDoesNotClaimUnchangedHold)
{
    auto request =
        pbc_json::parse(R"({"state":{"actions":{"controlled_companions":[{"id":"mira","holding_position":true}]}},
      "questions":{"intent1":{"criteria":{"decline_offer":"Decline offered item","stay":"Hold","keep_order":"Continue"}},
      "intent2":{"criteria":{"follow":"Follow"}},
      "recipient1":{"criteria":{"mira":"Mira"}},"recipient2":{"criteria":{"mira":"Mira"}},
      "service1":{"criteria":{"offer:123":"Mira's actual offer","NONE":"No offer"}},
      "second_action":{"criteria":{"YES":"Two operations","NONE":"One operation"}}}})");
    auto answers = pbc_json::parse(R"({"intent1":{"choice":"decline_offer"},"intent2":{"choice":"follow"},
      "recipient1":{"choice":"mira"},"recipient2":{"choice":"mira"},
      "service1":{"choice":"offer:123"},"second_action":{"choice":"NONE"}})");
    bool decline = false;
    for (auto const& [id, reading] : PBC::CompleteReadings(request, answers))
        if (!reading.choices.empty() && reading.choices.at("intent1") == "decline_offer")
        {
            decline |= reading.choices.at("service1") == "offer:123";
            if (reading.choices.at("service1") == "offer:123")
                EXPECT_NE(reading.description.find("Mira's actual offer"), std::string::npos);
        }
    EXPECT_TRUE(decline);
    answers["intent1"]["choice"] = "stay";
    answers["second_action"]["choice"] = "YES";
    for (auto const& [id, reading] : PBC::CompleteReadings(request, answers))
        if (!reading.choices.empty() && reading.choices.at("intent1") == "follow")
            EXPECT_EQ(reading.description.find("hold and its timer unchanged"), std::string::npos);
}

TEST(PBCModel, TerminalMovementEquivalencePreservesArrivalDependenciesAndBusyActors)
{
    auto state = pbc_json::parse(R"({"controlled_companions":[
      {"id":"mira","following_requester":true,"pending_gameplay":false},
      {"id":"sol","following_requester":true,"pending_gameplay":false}]})");
    std::map<std::string, PBC::InterpretationReading> readings = {{"a",
                                                                   {{{"intent1", "stay"},
                                                                     {"recipient1", "mira"},
                                                                     {"duration1", "INTERVAL"},
                                                                     {"intent2", "regroup"},
                                                                     {"recipient2", "mira"},
                                                                     {"second_action", "YES"}},
                                                                    {},
                                                                    ""}},
                                                                  {"b",
                                                                   {{{"intent1", "stay"},
                                                                     {"recipient1", "mira"},
                                                                     {"duration1", "INTERVAL"},
                                                                     {"intent2", "follow"},
                                                                     {"recipient2", "mira"},
                                                                     {"second_action", "YES"}},
                                                                    {},
                                                                    ""}},
                                                                  {"OTHER", {{}, {}, ""}}};
    auto answer = pbc_json::parse(R"({"choice":"a","probabilities":{"a":0.49,"b":0.48,"OTHER":0.03}})");
    EXPECT_FALSE(PBC::AmbiguousCompleteReading(readings, answer, state));
    for (auto key : {"a", "b"})
    {
        std::swap(readings[key].choices["intent1"], readings[key].choices["intent2"]);
        std::swap(readings[key].choices["duration1"], readings[key].choices["duration2"]);
    }
    EXPECT_TRUE(PBC::AmbiguousCompleteReading(readings, answer, state));
    readings["a"].choices = {{"intent1", "stay"},
                             {"recipient1", "mira"},
                             {"intent2", "follow"},
                             {"recipient2", "sol"},
                             {"second_action", "YES"}};
    readings["b"].choices = {{"intent1", "stay"}, {"recipient1", "mira"}, {"second_action", "NONE"}};
    EXPECT_FALSE(PBC::AmbiguousCompleteReading(readings, answer, state));
    state["controlled_companions"][1]["pending_gameplay"] = true;
    EXPECT_TRUE(PBC::AmbiguousCompleteReading(readings, answer, state));
}

TEST(PBCModel, ReturningMovementAndRepeatTargetsHaveConsistentWholeReadings)
{
    auto request = pbc_json::parse(R"({"state":{"actions":{"requester":"human",
      "controlled_companions":[{"id":"mira"}],"combat_reference_binding":{"intent":"attack","recipient":"mira","target":"boar"}}},
      "questions":{"intent1":{"criteria":{"approach":"Approach","regroup":"Return","attack":"Engage"}},
      "recipient1":{"criteria":{"mira":"Mira"}},
      "target1":{"criteria":{"NONE":"Implicit human","human":"Human","RECENT_COMBAT":"Grounded repeat","CLARIFY":"Ask"}},
      "reference1":{"criteria":{"NONE":"Implicit human","RECENT":"Witnessed reference","UNRESOLVED":"Ask"}},
      "second_action":{"criteria":{"NONE":"One operation"}}}})");
    auto answers = pbc_json::parse(R"({"intent1":{"choice":"approach"},"recipient1":{"choice":"mira"},
      "target1":{"choice":"human"},"reference1":{"choice":"NONE"},"second_action":{"choice":"NONE"}})");
    auto readings = PBC::CompleteReadings(request, answers);
    bool returns = false;
    for (auto const& [id, reading] : readings)
        if (!reading.choices.empty() && reading.choices.at("intent1") == "regroup")
        {
            returns = true;
            EXPECT_NE(reading.description.find("FOLLOWING"), std::string::npos);
        }
    EXPECT_TRUE(returns);
    answers["intent1"]["choice"] = "attack";
    answers["target1"]["choice"] = "CLARIFY";
    answers["reference1"]["choice"] = "UNRESOLVED";
    readings = PBC::CompleteReadings(request, answers);
    EXPECT_TRUE(std::any_of(readings.begin(), readings.end(),
                            [](auto const& entry)
                            {
                                auto const& choices = entry.second.choices;
                                return !choices.empty() && choices.at("intent1") == "attack" &&
                                       choices.at("target1") == "RECENT_COMBAT" && choices.at("reference1") == "RECENT";
                            }));
}

TEST(PBCModel, SequenceShortlistDoesNotInventZeroMassSecondOperations)
{
    auto request = pbc_json::parse(R"({"state":{"actions":{"controlled_companions":[{"id":"mira"}]}},
      "questions":{"intent1":{"criteria":{"follow":"Follow","resume":"Release"}},
      "intent2":{"criteria":{"follow":"Follow","NONE":"Absent"}},
      "recipient1":{"criteria":{"mira":"Mira"}},"recipient2":{"criteria":{"mira":"Mira"}},
      "second_action":{"criteria":{"NONE":"One operation","YES":"Two operations"}}}})");
    auto answers = pbc_json::parse(R"({"intent1":{"choice":"follow","probabilities":{"follow":0.6,"resume":0.4}},
      "intent2":{"choice":"NONE","probabilities":{"NONE":0.85,"follow":0.15}},
      "recipient1":{"choice":"mira"},"recipient2":{"choice":"mira"},
      "second_action":{"choice":"NONE","probabilities":{"NONE":1,"YES":0}}})");
    auto readings = PBC::CompleteReadings(request, answers);
    EXPECT_FALSE(
        std::any_of(readings.begin(), readings.end(), [](auto const& entry)
                    { return !entry.second.choices.empty() && entry.second.choices.at("second_action") == "YES"; }));
    answers["second_action"] = {{"choice", "YES"}, {"probabilities", {{"YES", 1}, {"NONE", 0}}}};
    readings = PBC::CompleteReadings(request, answers);
    EXPECT_TRUE(
        std::any_of(readings.begin(), readings.end(), [](auto const& entry)
                    { return !entry.second.choices.empty() && entry.second.choices.at("second_action") == "YES"; }));
}

TEST(PBCModel, JointEffectsGroupConcreteCombatBindingsWithoutChangingSelectedRequests)
{
    auto state = pbc_json::parse(R"({"controlled_companions":[{"id":"mira"}],
      "combat_reference_binding":{"intent":"attack","recipient":"mira","target":"boar"},"targets":[
      {"id":"human","friendly_to_requester":true,"attacking":"boar","attackers":["boar"]},
      {"id":"boar","alive":true,"attackable":true},
      {"id":"wolf","alive":true,"attackable":true}]})");
    auto make = [](std::string kind, std::string target)
    {
        return PBC::InterpretationReading{
            {{"intent1", kind}, {"recipient1", "mira"}, {"target1", target}, {"second_action", "NONE"}}, {}, ""};
    };
    std::map<std::string, PBC::InterpretationReading> readings = {
        {"attack", make("attack", "boar")},    {"assist", make("assist", "human")},
        {"protect", make("protect", "human")}, {"repeat", make("attack", "RECENT_COMBAT")},
        {"wrong", make("attack", "wolf")},     {"OTHER", {{}, {}, ""}}};
    auto answer = pbc_json::parse(R"({"choice":"assist","probabilities":
      {"assist":0.30,"attack":0.25,"protect":0.20,"repeat":0.10,"wrong":0.10,"OTHER":0.05}})");
    EXPECT_FALSE(PBC::AmbiguousCompleteReading(readings, answer, state));
    EXPECT_EQ(readings.at("assist").choices.at("intent1"), "assist");
    EXPECT_EQ(readings.at("assist").choices.at("target1"), "human");
    state["targets"][0]["attacking"] = "wolf";
    EXPECT_TRUE(PBC::AmbiguousCompleteReading(readings, answer, state));
    state["targets"][0]["attacking"] = "boar";
    state["targets"][0]["friendly_to_requester"] = false;
    EXPECT_TRUE(PBC::AmbiguousCompleteReading(readings, answer, state));
}

TEST(PBCModel, DuplicateHypothesesCannotAuthorizeANarrowTargetChoice)
{
    auto make = [](std::string target)
    {
        return PBC::InterpretationReading{
            {{"intent1", "attack"}, {"recipient1", "mira"}, {"target1", target}, {"second_action", "NONE"}}, {}, ""};
    };
    std::map<std::string, PBC::InterpretationReading> readings = {
        {"one", make("wolf")}, {"duplicate", make("wolf")}, {"ask", make("CLARIFY")}, {"OTHER", {{}, {}, ""}}};
    auto answer = pbc_json::parse(R"({"choice":"one","probabilities":
      {"one":0.31,"duplicate":0.20,"ask":0.30,"OTHER":0.19}})");
    EXPECT_TRUE(PBC::AmbiguousCompleteReading(readings, answer,
                                              pbc_json::parse(R"({"controlled_companions":[{"id":"mira"}]})")));
}

TEST(PBCModel, CombatReferenceEquivalenceRequiresItsOriginalActorAndOperation)
{
    auto state = pbc_json::parse(R"({"controlled_companions":[{"id":"mira"},{"id":"tess"}],
      "combat_reference_binding":{"intent":"attack","recipient":"mira","target":"boar"}})");
    auto make = [](std::string actor, std::string target)
    {
        return PBC::InterpretationReading{
            {{"intent1", "attack"}, {"recipient1", actor}, {"target1", target}, {"second_action", "NONE"}}, {}, ""};
    };
    auto answer = pbc_json::parse(R"({"choice":"direct","probabilities":
      {"direct":0.50,"repeat":0.48,"OTHER":0.02}})");
    std::map<std::string, PBC::InterpretationReading> readings = {
        {"direct", make("mira", "boar")}, {"repeat", make("mira", "RECENT_COMBAT")}, {"OTHER", {{}, {}, ""}}};
    EXPECT_FALSE(PBC::AmbiguousCompleteReading(readings, answer, state));
    readings["direct"] = make("tess", "boar");
    readings["repeat"] = make("tess", "RECENT_COMBAT");
    EXPECT_TRUE(PBC::AmbiguousCompleteReading(readings, answer, state));
}

TEST(PBCModel, NativeQuantityAliasesDoNotCauseAnUnnecessaryQuestion)
{
    auto make = [](std::string quantity)
    {
        return PBC::InterpretationReading{{{"intent1", "trade"},
                                           {"recipient1", "mira"},
                                           {"item1", "item:159"},
                                           {"quantity1", quantity},
                                           {"second_action", "NONE"}},
                                          {},
                                          ""};
    };
    std::map<std::string, PBC::InterpretationReading> readings = {
        {"count", make("NUM:2")}, {"pair", make("PAIR")}, {"one", make("NUM:1")}, {"OTHER", {{}, {}, ""}}};
    auto state = pbc_json::parse(R"({"controlled_companions":[{"id":"mira"}]})");
    auto answer = pbc_json::parse(R"({"choice":"count","probabilities":
      {"count":0.45,"pair":0.39,"one":0.12,"OTHER":0.04}})");
    EXPECT_FALSE(PBC::AmbiguousCompleteReading(readings, answer, state));
    readings["pair"].items = {{"item:159", 1}, {"item:117", 1}};
    EXPECT_TRUE(PBC::AmbiguousCompleteReading(readings, answer, state));
}

TEST(PBCModel, AssistanceReadingsNameTheirActualSubjectAndOpponentEvenWhenUnavailable)
{
    auto request = pbc_json::parse(R"({"state":{"actions":{"controlled_companions":[{"id":"mira"}],
      "targets":[{"id":"beast","label":"Boar","friendly_to_requester":false,"attacking":"human"},
                 {"id":"human","label":"Ari","friendly_to_requester":true,"attackers":["beast"]}]}},
      "questions":{"intent1":{"criteria":{"assist":"Assist a friendly ally","protect":"Protect an ally"}},
       "recipient1":{"criteria":{"mira":"Mira"}},"target1":{"criteria":{"beast":"Boar","human":"Ari"}},
       "reference1":{"criteria":{"IDENTIFIED":"Identified"}},"second_action":{"criteria":{"NONE":"Single"}}}})");
    auto answers = pbc_json::parse(R"({"intent1":{"choice":"assist"},"recipient1":{"choice":"mira"},
      "target1":{"choice":"beast"},"reference1":{"choice":"IDENTIFIED"},"second_action":{"choice":"NONE"}})");
    auto readings = PBC::CompleteReadings(request, answers);
    bool unavailableMeaningRetained = false;
    for (auto const& [id, reading] : readings)
        if (!reading.choices.empty() && reading.choices.at("intent1") == "assist" &&
            reading.choices.at("target1") == "beast")
        {
            unavailableMeaningRetained = true;
            EXPECT_NE(reading.description.find("Help Boar fight their current opponent"), std::string::npos);
            EXPECT_NE(reading.description.find("would_engage\":[\"Ari"), std::string::npos);
            EXPECT_NE(reading.description.find("subject_is_friendly_to_requester\":false"), std::string::npos);
        }
    EXPECT_TRUE(unavailableMeaningRetained);
}

// Opt-in experiment support: reconstruct the exact production menu and apply
// the real ambiguity policy to saved paid responses. This test never sends HTTP.
TEST(PBCModel, ExportAndScoreFrozenCompleteReadingEvidence)
{
    auto input = std::getenv("PBC_FROZEN_READING_INPUT");
    if (!input)
        GTEST_SKIP() << "Requires an isolated native capture directory; no provider calls";
    std::filesystem::path directory(input);
    auto read = [](std::filesystem::path const& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream bytes;
        bytes << file.rdbuf();
        return pbc_json::parse(bytes.str());
    };
    std::map<std::string, unsigned> cases;
    for (auto const& entry : std::filesystem::directory_iterator(directory))
        if (entry.path().filename().string().starts_with("case-") && entry.path().extension() == ".json")
        {
            auto record = read(entry.path());
            if (record.value("fixture_verified", false) && record.value("settled", false))
                cases[record.at("scene").get<std::string>()] = record.at("id").get<unsigned>();
        }
    ASSERT_LE(cases.size(), 30u);
    std::map<std::string, pbc_json> factors, coherent;
    for (auto const& entry : std::filesystem::directory_iterator(directory / "captures"))
        if (entry.path().extension() == ".json")
        {
            auto record = read(entry.path());
            auto scene = record.value("scene_id", "");
            if (!cases.contains(scene) || record.value("http_status", 0) != 200)
                continue;
            auto const& questions = record.at("request").at("questions");
            if (questions.contains("intent1"))
                factors[scene] = record;
            if (questions.contains("reading"))
                coherent[scene] = record;
        }
    pbc_json output = pbc_json::array();
    std::map<unsigned, std::map<std::string, PBC::InterpretationReading>> menus;
    std::map<unsigned, pbc_json> native;
    for (auto const& [scene, capture] : coherent)
    {
        ASSERT_TRUE(factors.contains(scene));
        auto const& first = factors.at(scene);
        auto readings = PBC::CompleteReadings(first.at("request"), first.at("response").at("answers"));
        pbc_json criteria = pbc_json::object(), serialized = pbc_json::object();
        for (auto const& [id, reading] : readings)
        {
            criteria[id] = reading.description;
            serialized[id] = {{"choices", reading.choices}, {"items", reading.items}};
        }
        ASSERT_EQ(criteria, capture.at("request").at("questions").at("reading").at("criteria"))
            << "The compiled interpreter differs from the captured production menu";
        auto id = cases.at(scene);
        native[id] = first.at("request").at("state").at("actions");
        menus[id] = std::move(readings);
        output.push_back({{"case_id", id},
                          {"source_request_id", capture.at("request_id")},
                          {"readings", serialized},
                          {"native_state", native.at(id)}});
    }
    ASSERT_FALSE(output.empty());
    auto save = [](std::filesystem::path const& path, pbc_json const& value)
    {
        std::ofstream file(path, std::ios::binary);
        file << value.dump(2) << '\n';
        file.close();
        std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    };
    save(directory / "reading-menus.json", output);
    if (auto responses = std::getenv("PBC_FROZEN_READING_RESPONSES"))
    {
        pbc_json scores = pbc_json::array();
        for (auto const& entry : std::filesystem::directory_iterator(responses))
        {
            if (entry.path().extension() != ".json" || entry.path().filename() == "summary.json" ||
                entry.path().filename() == "cpp-scores.json")
                continue;
            auto record = read(entry.path());
            if (!record.contains("variant"))
                continue;
            auto caseId = record.at("case_id").get<unsigned>();
            ASSERT_TRUE(menus.contains(caseId));
            ASSERT_TRUE(record.value("valid", false));
            auto const& answer = record.at("response").at("answers").at("reading");
            auto id = answer.at("choice").get<std::string>();
            auto const& readings = menus.at(caseId);
            ASSERT_TRUE(readings.contains(id));
            bool ambiguous = PBC::AmbiguousCompleteReading(readings, answer, native.at(caseId));
            if (ambiguous)
                id = "OTHER";
            auto const& reading = readings.at(id);
            scores.push_back({{"case_id", caseId},
                              {"variant", record.at("variant")},
                              {"ambiguous", ambiguous},
                              {"choice", id},
                              {"decisions", reading.choices},
                              {"items", reading.items},
                              {"meaning", reading.description}});
        }
        save(std::filesystem::path(responses) / "cpp-scores.json", scores);
    }
}
