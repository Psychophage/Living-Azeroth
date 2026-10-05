// SPDX-License-Identifier: GPL-2.0-or-later
// PBC Character System changes, 2026-09-30; upstream attribution in NOTICE.md.

#ifndef PBC_MODEL_H
#define PBC_MODEL_H

#include "pbc_budget.h"
#include "pbc_http.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace PBC
{
struct ModelSettings
{
    std::string selectorInstructions;
    std::string apiKey;
    std::string selectorApiKey;
    // Local backends are explicitly self-hosted, with zero provider API charges.
    // These settings do not infer free usage from a missing paid-provider cost.
    std::string chatBackend = "openrouter";
    std::string selectorBackend = "openrouter";
    std::string chatUrl = "https://openrouter.ai/api/v1/chat/completions";
    std::string decisionUrl = "https://openrouter.ai/api/alpha/decisions";
    std::string dialogueModel = "z-ai/glm-5.3-flash";
    std::string selectorModel = "typesafe/jev-1.13";
    uint32_t contextTokens = 32000;
    uint32_t outputTokens = 2048;
    uint32_t selectorTokens = 8000;
    uint32_t raidSelectorTokens = 32000;
    uint64_t inputNanoPerToken = 150;
    uint64_t outputNanoPerToken = 500;
    uint64_t selectorNanoPerToken = 42;
    uint32_t timeoutSeconds = 45;
    // Opt-in development capture. Contains witnessed conversation/private routing
    // context; keep outside public exports and leave disabled on ordinary realms.
    std::string selectorCapturePath;
    uint32_t selectorCaptureMaxRecords = 256;
    uint64_t selectorCaptureMaxBytes = 33554432;
};

struct ModelReply
{
    bool success = false;
    bool billingKnown = false;
    int httpStatus = 0;
    std::string text;
    std::string requestId;
    std::string error;
    std::map<std::string, std::string> decisions;
    // Preserve provider evidence without treating concentration as correctness.
    // Raw typed answers include speaker and action probabilities/confidence.
    std::string decisionAnswersJson;
    std::map<std::string, std::map<std::string, uint32_t>> itemSets;
};

struct SpeakerCandidate
{
    std::string id;
    std::string summary;  // Public/scene-appropriate summary, never another
                          // actor's private recall.
    bool addressed = false;
    std::string privateSelectionCues = {};  // Speaker question only; never shared action state or another reply.
};

struct SelectionState
{
    std::string contribution;
    std::vector<std::string> transcript;  // Delivered lines only, oldest first,
                                          // same permitted audience.
    std::vector<SpeakerCandidate> candidates;
    std::string previousSpeaker;
    uint8_t remainingTurns = 0;
    // Deduplicated dialogue heard by the human and at least one eligible speaker.
    struct Observation
    {
        std::string text;
        std::vector<std::string> heardBy;
    };
    std::vector<Observation> recentContext;
    bool ambient = false;
    struct Question
    {
        std::string id;
        std::string instructions;
        std::map<std::string, std::string> criteria;
    };
    // Request interpretation uses public/native facts only. Private cues stay in
    // speaker criteria.
    std::string actionFactsJson = "{}";
    std::vector<Question> questions;
};

using HttpTransport = std::function<PBC_HttpResponse(
    std::string const&, std::string const&, std::vector<std::pair<std::string, std::string>> const&, uint32_t)>;

// Synchronous worker API. It never retries; the conversation owner decides
// whether one additional DIRECT attempt is justified. Unknown billing is never
// retried here.
class ModelGateway
{
public:
    ModelGateway(ModelSettings settings, ApiLedger& ledger, HttpTransport transport = {});
    ModelReply Generate(std::string const& system, std::string const& context, std::string const& schema,
                        ApiReservation request, uint32_t outputTokens = 0);
    ModelReply Select(SelectionState const& state, ApiReservation request);
    ModelSettings const& Settings() const { return _settings; }

private:
    ModelReply Submit(std::string const& body, bool selector, ApiReservation request, uint32_t outputTokens = 0);
    ModelSettings _settings;
    ApiLedger& _ledger;
    HttpTransport _transport;
};
}  // namespace PBC

#endif
