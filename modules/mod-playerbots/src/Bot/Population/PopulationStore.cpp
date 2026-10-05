/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth population: persistent identities, familiarity and offline allowance.
#include <algorithm>
#include <ctime>

#include "AccountMgr.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "PlayerbotsDatabase.h"
#include "PopulationMgr.h"

using namespace RealmPopulation;

void PlayerbotPopulationMgr::Configure()
{
    _settings = sPlayerbotAIConfig.population;
    _enabled.store(_settings.enabled);
}

void PlayerbotPopulationMgr::Initialize()
{
    if (!Enabled() || _loaded)
        return;
    Load();
    LoadZoneLevels();
    _loaded = true;
    _world.lastTick = uint64_t(time(nullptr));
    Publish({}, _world.lastTick);
    LOG_INFO("playerbots",
             "Population loaded: {} persistent characters, {} familiar relationships, {} zone level ranges, {} "
             "starting areas",
             _bots.size(), _familiarity.size(), _zoneLevels.size(), _starterAreas.size());
}

void PlayerbotPopulationMgr::Load()
{
    auto rows = PlayerbotsDatabase.Query(PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_SEL_POPULATION_CHARACTERS));
    if (rows)
    {
        do
        {
            auto f = rows->Fetch();
            Record record;
            record.character.id = f[0].Get<uint32>();
            record.account = f[1].Get<uint32>();
            record.plannedLevel = f[2].Get<uint32>();
            record.character.initialized = f[3].Get<bool>();
            record.character.introduced = f[4].Get<bool>();
            record.character.map = f[5].Get<uint32>();
            record.character.zone = f[6].Get<uint32>();
            record.character.sessionStarted = f[7].Get<uint64>();
            record.character.restUntil = f[8].Get<uint64>();
            record.character.lastSeen = f[9].Get<uint64>();
            _bots.emplace(record.character.id, record);
        } while (rows->NextRow());
    }
    rows = PlayerbotsDatabase.Query(PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_SEL_POPULATION_FAMILIARITY));
    if (rows)
    {
        do
        {
            auto f = rows->Fetch();
            Familiarity value;
            value.score = f[2].Get<uint32>();
            value.sightings = f[3].Get<uint32>();
            value.sessionSightings = f[4].Get<uint32>();
            value.interactions = f[5].Get<uint32>();
            value.lastSeen = f[6].Get<uint64>();
            value.lastSighting = f[7].Get<uint64>();
            value.lastInteraction = f[8].Get<uint64>();
            value.lastSession = f[9].Get<uint64>();
            _familiarity.emplace(Policy::Pair(f[0].Get<uint32>(), f[1].Get<uint32>()), value);
        } while (rows->NextRow());
    }
    rows = PlayerbotsDatabase.Query(PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_SEL_POPULATION_WORLD));
    if (rows)
    {
        _world.offlineRemaining = std::min<uint64_t>(_settings.offlineAllowance, rows->Fetch()[0].Get<uint64>());
        _world.nextCreation = rows->Fetch()[1].Get<uint64>();
        uint64_t checkpoint = rows->Fetch()[2].Get<uint64>();
        uint64_t now = time(nullptr);
        if (checkpoint && now > checkpoint)
            _world.offlineRemaining -= std::min(_world.offlineRemaining, now - checkpoint);
    }

    // Account assignments include dormant characters beyond today's reserve target. Import
    // existing identities conservatively: no level resets or assumptions about prior history.
    rows = PlayerbotsDatabase.Query(PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_SEL_ACCOUNT_TYPE));
    std::unordered_set<uint32_t> accounts;
    if (rows)
    {
        do
        {
            auto f = rows->Fetch();
            if (f[1].Get<uint8>() == 1)
                accounts.insert(f[0].Get<uint32>());
        } while (rows->NextRow());
    }
    for (auto const& entry : _bots)
        accounts.insert(entry.second.account);
    for (uint32_t account : accounts)
    {
        std::string name;
        if (!AccountMgr::GetName(account, name))
            continue;
        auto statement = CharacterDatabase.GetPreparedStatement(CHAR_SEL_ACCOUNT_INFO_CHARS);
        statement->SetData(0, account);
        auto characters = CharacterDatabase.Query(statement);
        _accounts.push_back(account);
        if (std::find(sPlayerbotAIConfig.randomBotAccounts.begin(), sPlayerbotAIConfig.randomBotAccounts.end(),
                      account) == sPlayerbotAIConfig.randomBotAccounts.end())
            sPlayerbotAIConfig.randomBotAccounts.push_back(account);
        if (!characters)
            continue;
        do
        {
            auto f = characters->Fetch();
            uint32_t id = f[0].Get<uint32>();
            bool imported = !_bots.count(id);
            Record& record = _bots[id];
            record.character.id = id;
            record.account = account;
            record.playerClass = f[4].Get<uint8>();
            record.race = f[3].Get<uint8>();
            record.character.faction = IsAlliance(record.race) ? 0 : 1;
            if (imported)
            {
                record.character.introduced = true;
                record.character.initialized = true;
                record.plannedLevel = f[2].Get<uint8>();
            }
            record.character.level = record.character.initialized ? f[2].Get<uint8>() : record.plannedLevel;
            _accountClasses[account].insert(record.playerClass);
            if (imported)
                Save(record);
        } while (characters->NextRow());
    }
    // A manually deleted disposable character must not occupy a population seat forever.
    for (auto it = _bots.begin(); it != _bots.end();)
        if (!it->second.playerClass)
            it = _bots.erase(it);
        else
            ++it;
    // Offline explicit friendships also survive a crash before the next encounter tick.
    // This is a startup-only scan, never a map-thread or per-bot SQL query.
    auto friends = CharacterDatabase.Query("SELECT guid, friend FROM character_social WHERE flags & 1");
    if (friends)
        do
        {
            auto f = friends->Fetch();
            uint32_t human = f[0].Get<uint32>(), bot = f[1].Get<uint32>();
            auto it = _bots.find(bot);
            auto cached = sCharacterCache->GetCharacterCacheByGuid(ObjectGuid::Create<HighGuid::Player>(human));
            if (it == _bots.end() || !cached || sPlayerbotAIConfig.IsInRandomAccountList(cached->AccountId))
                continue;
            auto key = Policy::Pair(human, bot);
            auto& relation = _familiarity[key];
            if (relation.score < _settings.familiarityThreshold)
            {
                relation.score = _settings.familiarityThreshold;
                SaveFamiliarity(key, relation);
            }
            if (!it->second.character.introduced)
            {
                it->second.character.introduced = true;
                Save(it->second);
            }
        } while (friends->NextRow());
    std::sort(_accounts.begin(), _accounts.end());
}

void PlayerbotPopulationMgr::Save(Record const& record)
{
    auto statement = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_REP_POPULATION_CHARACTER);
    auto const& c = record.character;
    statement->SetData(0, c.id);
    statement->SetData(1, record.account);
    statement->SetData(2, record.plannedLevel);
    statement->SetData(3, uint8_t(c.initialized));
    statement->SetData(4, uint8_t(c.introduced));
    statement->SetData(5, c.map);
    statement->SetData(6, c.zone);
    statement->SetData(7, c.sessionStarted);
    statement->SetData(8, c.restUntil);
    statement->SetData(9, c.lastSeen);
    // World-thread transition checkpoint: complete before admitting/logging out this identity.
    PlayerbotsDatabase.Execute(statement);
}

void PlayerbotPopulationMgr::SaveFamiliarity(uint64_t key, Familiarity const& value)
{
    auto statement = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_REP_POPULATION_FAMILIARITY);
    statement->SetData(0, uint32_t(key >> 32));
    statement->SetData(1, uint32_t(key));
    statement->SetData(2, value.score);
    statement->SetData(3, value.sightings);
    statement->SetData(4, value.sessionSightings);
    statement->SetData(5, value.interactions);
    statement->SetData(6, value.lastSeen);
    statement->SetData(7, value.lastSighting);
    statement->SetData(8, value.lastInteraction);
    statement->SetData(9, value.lastSession);
    PlayerbotsDatabase.Execute(statement);
}

bool PlayerbotPopulationMgr::SaveWorld()
{
    auto statement = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_REP_POPULATION_WORLD);
    statement->SetData(0, _world.offlineRemaining);
    statement->SetData(1, _world.nextCreation);
    statement->SetData(2, _world.lastTick);
    PlayerbotsDatabase.Execute(statement);
    auto saved = PlayerbotsDatabase.Query(PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_SEL_POPULATION_WORLD));
    _persistenceHealthy = saved && saved->Fetch()[0].Get<uint64>() == _world.offlineRemaining &&
                          saved->Fetch()[1].Get<uint64>() == _world.nextCreation &&
                          saved->Fetch()[2].Get<uint64>() == _world.lastTick;
    if (!_persistenceHealthy)
        LOG_ERROR("playerbots", "Population checkpoint failed; creation and autonomous activity paused");
    return _persistenceHealthy;
}
