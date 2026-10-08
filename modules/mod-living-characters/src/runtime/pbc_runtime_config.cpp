// SPDX-License-Identifier: GPL-2.0-or-later
// PBC Character System changes, 2026-09-30; upstream attribution in NOTICE.md.

#include "Config.h"
#include "CryptoRandom.h"
#include "PlayerbotDialogue.h"
#include "pbc_json.h"
#include "pbc_log.h"
#include "pbc_recorded.h"
#include "pbc_runtime_internal.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

namespace PBC::RuntimeDetail
{
uint64_t EpochMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string Identifier()
{
    std::string result;
    for (auto byte : Acore::Crypto::GetRandomBytes<16>())
    {
        result += "0123456789abcdef"[byte >> 4];
        result += "0123456789abcdef"[byte & 15];
    }
    return result;
}

std::string ReadText(std::string const& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

std::string ReadPrompt(std::string const& path, std::string const& name)
{
    auto custom = ReadText(path + "/" + name + ".custom.txt");
    return custom.empty() ? ReadText(path + "/" + name + ".default.txt") : custom;
}

bool Runtime::Start()
{
    if (sConfigMgr->GetOption<bool>("LLMChatter.Enable", false) ||
        sConfigMgr->GetOption<bool>("AiPlayerbot.EnableAutoTradeOnItemMention", true) ||
        sConfigMgr->GetOption<std::string>("AiPlayerbot.CommandPrefix", "") != "@")
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR,
                "Disable competing LLM Chatter generation and use "
                "explicit Playerbots prefix '@' and automatic item-mention trading "
                "disabled.");
        return false;
    }
    auto database = sConfigMgr->GetOption<std::string>("CharacterDatabaseInfo", "");
    auto budgetDatabase = sConfigMgr->GetOption<std::string>("PBC.CharacterSystem.BudgetDatabaseInfo", database);
    if (budgetDatabase.empty())
        budgetDatabase = database;
    auto budget = sConfigMgr->GetOption<std::string>("PBC.CharacterSystem.BudgetId", "");
    auto path = sConfigMgr->GetOption<std::string>("PBC.CharacterSystem.PromptsPath", "");
    auto keyPath = sConfigMgr->GetOption<std::string>("PBC.CharacterSystem.KeyFile", "");
    auto fixturePath = sConfigMgr->GetOption<std::string>("PBC.CharacterSystem.RecordedFixture", "", false);
    _realmPhase = sConfigMgr->GetOption<std::string>("PBC.CharacterSystem.RealmPhase", "");
    if (_realmPhase.empty())
        _realmPhase = ReadText(sConfigMgr->GetOption<std::string>("PBC.CharacterSystem.RealmPhaseFile", ""));
    auto knowledgePath = sConfigMgr->GetOption<std::string>("PBC.CharacterSystem.KnowledgePath", "");
    _knowledgeBytes = std::clamp(sConfigMgr->GetOption<uint32_t>("PBC.CharacterSystem.KnowledgeBytes", 3600), 512u, 8000u);
    if (!knowledgePath.empty())
    {
        std::string error;
        if (!_knowledge.Load(ReadText(knowledgePath),
            sConfigMgr->GetOption<std::string>("PBC.CharacterSystem.KnowledgeEra", ""), error))
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "Character knowledge rejected: {}", error);
            return false;
        }
    }
    _spacing = std::clamp(sConfigMgr->GetOption<uint32_t>("PBC.CharacterSystem.SpacingMs", 2000), 250u, 10000u);
    _readingWordsPerMinute = sConfigMgr->GetOption<uint32_t>("PBC.CharacterSystem.ReadingWordsPerMinute", 150);
    if (_readingWordsPerMinute || fixturePath.empty())
        _readingWordsPerMinute = std::clamp(_readingWordsPerMinute, 120u, 600u);
    _maxTurns = std::clamp(sConfigMgr->GetOption<uint8_t>("PBC.CharacterSystem.MaxTurns", 4), uint8_t{1}, uint8_t{8});
    _memoryInactiveMs =
        std::clamp(sConfigMgr->GetOption<uint32_t>("PBC.CharacterSystem.MemoryInactiveSeconds", 300), 300u, 3600u) *
        1000;
    _memoryTokens = std::clamp(sConfigMgr->GetOption<uint32_t>("PBC.CharacterSystem.MemoryTokens", 4096), 512u, 16000u);
    _ambientEnabled = sConfigMgr->GetOption<bool>("PBC.CharacterSystem.AmbientEnabled", true);
    _combatEnabled = sConfigMgr->GetOption<bool>("PBC.CharacterSystem.CombatBanterEnabled", true);
    _combatChance = std::min(sConfigMgr->GetOption<uint32_t>("PBC.CharacterSystem.CombatBanterChance", 10), 100u);
    _combatCooldown =
        std::clamp(sConfigMgr->GetOption<uint32_t>("PBC.CharacterSystem.CombatBanterCooldownSeconds", 180), 60u,
                   3600u) *
        1000;
    _ambientMinimum =
        std::clamp(sConfigMgr->GetOption<uint32_t>("PBC.CharacterSystem.AmbientMinMinutes", 20), 5u, 240u) * 60000;
    _ambientMaximum = std::clamp(sConfigMgr->GetOption<uint32_t>("PBC.CharacterSystem.AmbientMaxMinutes", 40),
                                 _ambientMinimum / 60000, 240u) *
                      60000;
    if (auto seed = sConfigMgr->GetOption<uint32_t>("PBC.CharacterSystem.RandomSeed", 0))
        _random.seed(seed);
    _nextAmbient = 0;  // each player's first remark comes one interval after they are first seen
    CharacterPrompts prompts{ReadPrompt(path, "Character.system"), ReadPrompt(path, "Foundation.system"),
                             ReadPrompt(path, "Memory.system"), ReadPrompt(path, "Recall.system")};
    ModelSettings settings;
    settings.chatBackend = sConfigMgr->GetOption<std::string>("PBC.CharacterSystem.ChatBackend", "openrouter");
    settings.selectorBackend = sConfigMgr->GetOption<std::string>("PBC.CharacterSystem.SelectorBackend", "openrouter");
    settings.chatUrl = sConfigMgr->GetOption<std::string>("PBC.CharacterSystem.ChatUrl", settings.chatUrl);
    settings.decisionUrl = sConfigMgr->GetOption<std::string>("PBC.CharacterSystem.SelectorUrl", settings.decisionUrl);
    settings.dialogueModel =
        sConfigMgr->GetOption<std::string>("PBC.CharacterSystem.DialogueModel", settings.dialogueModel);
    settings.selectorModel =
        sConfigMgr->GetOption<std::string>("PBC.CharacterSystem.SelectorModel", settings.selectorModel);
    settings.inputNanoPerToken =
        sConfigMgr->GetOption<uint64_t>("PBC.CharacterSystem.InputNanoPerToken", settings.inputNanoPerToken);
    settings.outputNanoPerToken =
        sConfigMgr->GetOption<uint64_t>("PBC.CharacterSystem.OutputNanoPerToken", settings.outputNanoPerToken);
    settings.selectorNanoPerToken =
        sConfigMgr->GetOption<uint64_t>("PBC.CharacterSystem.SelectorNanoPerToken", settings.selectorNanoPerToken);
    settings.selectorInstructions = ReadPrompt(path, "Selector.system");
    settings.selectorCapturePath =
        sConfigMgr->GetOption<std::string>("PBC.CharacterSystem.SelectorCapturePath", "", false);
    settings.selectorCaptureMaxRecords =
        std::min(sConfigMgr->GetOption<uint32_t>("PBC.CharacterSystem.SelectorCaptureMaxRecords", 256, false), 4096u);
    settings.selectorCaptureMaxBytes =
        std::min(sConfigMgr->GetOption<uint64_t>("PBC.CharacterSystem.SelectorCaptureMaxBytes", 33554432, false),
                 uint64_t{268435456});
    settings.selectorTokens =
        std::clamp(sConfigMgr->GetOption<uint32_t>("PBC.CharacterSystem.SelectorContextTokens", 8000), 2000u, 32000u);
    settings.raidSelectorTokens =
        std::clamp(sConfigMgr->GetOption<uint32_t>("PBC.CharacterSystem.RaidSelectorContextTokens", 32000),
                   settings.selectorTokens, 32000u);
    settings.apiKey = ReadText(keyPath);
    while (!settings.apiKey.empty() && (settings.apiKey.back() == '\n' || settings.apiKey.back() == '\r'))
        settings.apiKey.pop_back();
    settings.selectorApiKey = ReadText(sConfigMgr->GetOption<std::string>("PBC.CharacterSystem.SelectorKeyFile", ""));
    while (!settings.selectorApiKey.empty() &&
           (settings.selectorApiKey.back() == '\n' || settings.selectorApiKey.back() == '\r'))
        settings.selectorApiKey.pop_back();
    settings.contextTokens =
        std::clamp(sConfigMgr->GetOption<uint32_t>("PBC.CharacterSystem.ContextTokens", 32000), 8000u, 32000u);
    HttpTransport transport;
    if (!fixturePath.empty())
    {
        // Explicitly isolated from real credentials and the development paid
        // ledger.
        if (!keyPath.empty() || !settings.selectorApiKey.empty() || !budget.starts_with("recorded-"))
            return false;
        try
        {
            transport = RecordedTransport(ReadText(fixturePath));
        }
        catch (std::exception const&)
        {
            return false;
        }
        settings.apiKey = "recorded-no-network";
        _recorded = true;
    }
    bool chatCredential = settings.chatBackend == "openai-local" || !settings.apiKey.empty();
    bool selectorCredential = settings.selectorBackend == "systemone-local" || !settings.selectorApiKey.empty() ||
                              (settings.chatBackend == "openrouter" && !settings.apiKey.empty());
    if (database.empty() || budget.empty() || !chatCredential || !selectorCredential || _realmPhase.empty() ||
        prompts.dialogue.empty() || prompts.foundation.empty() || prompts.memory.empty() || prompts.recall.empty() ||
        settings.selectorInstructions.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR,
                "Character system configuration is incomplete; generation remains "
                "disabled.");
        return false;
    }
    auto identitiesPath = sConfigMgr->GetOption<std::string>("PBC.CharacterSystem.NpcIdentities", "");
    if (!identitiesPath.empty())
    {
        auto identities = pbc_json::parse(ReadText(identitiesPath), nullptr, false);
        if (!identities.is_array())
            return false;
        try
        {
            for (auto const& identity : identities)
                _definitions.emplace(
                    std::tuple(identity.at("spawn_id").get<uint32_t>(), identity.at("map_id").get<uint32_t>(),
                               identity.at("instance_id").get<uint32_t>()),
                    NpcDefinition{identity.value("actor_id", ""), identity.value("canon", ""),
                                  identity.value("watch_id", "")});
        }
        catch (pbc_json::exception const&)
        {
            return false;
        }
    }
    _storage = std::make_unique<Worker>();
    _inference = std::make_unique<Worker>();
    if (!_storage->Submit([&] { return _store.Open(database); }).get() ||
        !_inference->Submit([&] { return _ledger.Open(budgetDatabase, budget) && _ledger.Totals().has_value(); }).get())
        return false;
    _model = std::make_unique<ModelGateway>(std::move(settings), _ledger, std::move(transport));
    _characters = std::make_unique<CharacterService>(_store, *_model, _ledger, std::move(prompts));
    if (PlayerbotDialogueBridge::Enabled() &&
        _storage
            ->Submit([this, database]
                     { return _actionStore.Open(database) && _actionStore.Recover(_runId, _epochStart); })
            .get())
        _actions = std::make_unique<ActionCoordinator>(ActionCallbacks{
            [this](ActionRequest const& request) { return PrepareAction(request); },
            [capture =
                 !sConfigMgr->GetOption<std::string>("PBC.CharacterSystem.SelectorCapturePath", "", false).empty()](
                ActionRequest const& request)
            {
                auto accepted = PlayerbotDialogueBridge::Enqueue(request);
                if (capture)
                    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Character dispatch {}: accepted={}, epoch_ms={}.", request.id,
                            accepted, EpochMs());
                return accepted;
            },
            [](ActionRequest const& request) { PlayerbotDialogueBridge::Cancel(request.actor.guid, request.id); },
            [this](ActionRequest const& request, ActionOutcome const& outcome) { ActionResult(request, outcome); }});
    else if (PlayerbotDialogueBridge::Enabled())
        PBC_Log(PBC_LogLevel::PBC_ERROR,
                "Dialogue actions are unavailable: the "
                "native-action journal could not open.");
    // Living Azeroth: written guild identities, kept on the world thread for members' context.
    auto& identities = GuildIdentities();
    identities.clear();
    for (auto const& [guild, stored] : _storage->Submit([this] { return _store.GuildIdentities(); }).get())
    {
        GuildIdentity identity;
        std::string error;
        if (ApplyGuildIdentityJson(identity, pbc_json::parse(stored, nullptr, false), error))
            identities[guild] = identity;
        else
            PBC_Log(PBC_LogLevel::PBC_ERROR, "Guild {} identity was not usable: {}.", guild, error);
    }
    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Character coordinator started; one shared spending ledger.");
    return true;
}

}  // namespace PBC::RuntimeDetail
