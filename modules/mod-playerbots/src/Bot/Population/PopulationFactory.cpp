/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth population: gradual native character creation and unseen reserve adaptation.
#include <algorithm>
#include <limits>

#include "AccountMgr.h"
#include "CharacterCache.h"
#include "CryptoRandom.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "PlayerbotsDatabase.h"
#include "PopulationMgr.h"
#include "Random.h"
#include "RaceMgr.h"
#include "RandomPlayerbotFactory.h"
#include "World.h"
#include "WorldSession.h"

using namespace RealmPopulation;

void PlayerbotPopulationMgr::AdaptReserve(std::vector<Place> const& places, uint64_t now)
{
    auto accepting = [&](uint32_t faction, uint32_t map, uint32_t zone)
    {
        return std::any_of(places.begin(), places.end(), [&](Place const& place)
                           { return place.accepting && place.faction == faction && place.map == map && place.zone == zone; });
    };
    // Planned strangers whose place no longer takes arrivals return to the reserve.
    for (auto& [id, record] : _bots)
        if (record.demandHuman && !record.character.online && !record.character.introduced &&
            !accepting(record.character.faction, record.character.map, record.character.zone))
            record.demandHuman = 0;
    uint32_t adapted = 0;
    uint32_t cap = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    // Places players are in come first; capitals' standing residents take what is left.
    std::vector<Place const*> demands;
    for (bool residents : {false, true})
    {
        size_t tier = demands.size();
        for (auto const& place : places)
            if (place.accepting && place.resident == residents)
                demands.push_back(&place);
        if (demands.size() > tier)
            std::rotate(demands.begin() + tier,
                        demands.begin() + tier + (now / _settings.creationSeconds) % (demands.size() - tier),
                        demands.end());
    }
    for (Place const* place : demands)
    {
        uint32_t quota = Policy::Quota(*place, _settings);
        // Count only people who can be here now: resting or finished characters need replacing.
        uint32_t available = 0;
        for (auto const& [id, record] : _bots)
            if (record.character.faction == place->faction && record.character.map == place->map &&
                record.character.zone == place->zone && Policy::CanBePresent(record.character, _settings, now) &&
                (Policy::Fits(*place, record.character.level, false, _settings) ||
                 Policy::Fits(*place, record.character.level, true, _settings)))
                ++available;
        for (auto& [id, record] : _bots)
        {
            auto& c = record.character;
            if (adapted >= _settings.creationBatch || available >= quota)
                break;
            // Rest models absence of people a player knows; a never-introduced stranger can
            // be re-planned for local demand immediately.
            if (c.faction != place->faction || c.introduced || c.online || c.engaged ||
                record.loadingUntil > now || record.protectedUntil > now || record.demandHuman)
                continue;
            auto cached = sCharacterCache->GetCharacterCacheByGuid(ObjectGuid::Create<HighGuid::Player>(id));
            if (!cached || cached->MailCount || !cached->GroupGuid.IsEmpty())
                continue;
            bool peerSeat = Policy::PeerSeat(*place, available, _settings);
            uint32_t level = Policy::PlanLevel(*place, peerSeat, Policy::Mix(id ^ available), cap, _settings);
            if (!Policy::RaceFits(*place, record.race, level))
                continue;  // a young character of another race does not start here
            if (record.playerClass == CLASS_DEATH_KNIGHT &&
                level < sWorld->getIntConfig(CONFIG_START_HEROIC_PLAYER_LEVEL))
                continue;
            // Reserve adjustment only touches an identity never introduced to any human.
            // Names/GUIDs remain stable; known/history-bearing actors never enter this path.
            record.plannedLevel = level;
            c.level = level;
            c.initialized = false;
            c.restUntil = 0;
            record.demandHuman = place->humans.empty() ? 0 : place->humans.front().id;
            c.map = place->map;
            c.zone = place->zone;
            Save(record);
            ++adapted;
            ++available;
        }
    }
}

void PlayerbotPopulationMgr::CreateBatch(std::vector<Place> const& places, bool humansPresent, uint64_t now)
{
    if (_pendingCreations)
        return;  // Do not overfill the reserve while character writes are in flight.
    if (!_pendingAccount.empty())
    {
        uint32_t account = AccountMgr::GetId(_pendingAccount);
        if (!account)
            return;  // Never create another account while the previous write is unresolved.
        _accounts.push_back(account);
        sPlayerbotAIConfig.randomBotAccounts.push_back(account);
        auto statement = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_INS_ACCOUNT_TYPE);
        statement->SetData(0, account);
        statement->SetData(1, uint8_t(1));
        PlayerbotsDatabase.Execute(statement);
        _pendingAccount.clear();
    }
    if (now < _world.nextCreation)
        return;
    uint32_t availableRoster = 0;
    for (auto const& [id, record] : _bots)
        if (!record.character.introduced || record.character.online || record.loadingUntil > now)
            ++availableRoster;
    // Dormant established identities remain stored outside the reserve target, leaving
    // room for genuine newcomers after years of relationships, without deleting anyone.
    uint32_t count = Policy::CreationCount(availableRoster, humansPresent, _world, _settings, now);
    _world.nextCreation = now + _settings.creationSeconds;
    if (!SaveWorld())  // A crash/restart cannot repeatedly obtain an extra creation batch.
        return;
    if (!count)
        return;
    RandomPlayerbotFactory factory;
    std::unordered_map<RandomPlayerbotFactory::NameRaceAndGender, std::vector<std::string>> names;
    uint32_t created = 0;
    for (uint32_t attempt = 0; attempt < count; ++attempt)
    {
        uint32_t account = 0;
        uint8_t playerClass = 0;
        for (uint32_t candidate : _accounts)
        {
            for (uint8_t cls = CLASS_WARRIOR; cls < MAX_CLASSES; ++cls)
            {
                if (!((1u << (cls - 1)) & CLASSMASK_ALL_PLAYABLE) || !sChrClassesStore.LookupEntry(cls) ||
                    ((1u << (cls - 1)) & sWorld->getIntConfig(CONFIG_CHARACTER_CREATING_DISABLED_CLASSMASK)))
                    continue;
                if (!_accountClasses[candidate].count(cls))
                {
                    account = candidate;
                    playerClass = cls;
                    break;
                }
            }
            if (account)
                break;
        }
        if (!account)
        {
            std::string name;
            uint32_t index = _accounts.size();
            do
            {
                // This core limits account names to 17 characters. Reserve room for
                // the population suffix and the configured maximum roster's index.
                name = sPlayerbotAIConfig.randomBotAccountPrefix.substr(0, 8) + "pop" + std::to_string(index++);
            } while (AccountMgr::GetId(name));
            // Bot-only accounts use an unreported random password, irrespective of legacy defaults.
            std::string password;
            constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
            for (uint8_t byte : Acore::Crypto::GetRandomBytes<MAX_PASS_STR>())
                password.push_back(alphabet[byte & 31]);
            auto result = sAccountMgr->CreateAccount(name, password);
            if (result != AOR_OK)
            {
                LOG_ERROR("playerbots", "Population could not create bot account {} (result {})", name,
                          uint32_t(result));
                break;
            }
            _pendingAccount = name;
            // The account API enqueues its SQL. Resolve it on the next manager tick,
            // rather than assuming a synchronous query already sees the new account.
            break;
        }
        // Decide the place before creation so the newcomer is of that place's faction.
        Place const* regional = nullptr;
        std::vector<Place const*> open;
        for (bool residents : {false, true})
            if (open.empty())
                for (auto const& place : places)
                    if (place.accepting && place.resident == residents)
                        open.push_back(&place);
        if (!open.empty() && urand(0, 99) < _settings.regionalPercent)
            regional = open[urand(0, open.size() - 1)];
        // Plan the newcomer's level first: below the starter level only the place's own races belong.
        uint32_t cap = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
        uint32_t seed = urand(0, std::numeric_limits<int32>::max());
        uint32_t plannedLevel =
            regional ? Policy::PlanLevel(*regional, Policy::PeerSeat(*regional, attempt, _settings), seed, cap, _settings) : 0;
        uint32_t raceMask = 0;
        if (regional)
            for (uint8 race = RACE_HUMAN; race < sRaceMgr->GetMaxRaces(); ++race)
                if (IsAlliance(race) == !regional->faction && Policy::RaceFits(*regional, race, plannedLevel))
                    raceMask |= 1u << (race - 1);
        WorldSession session(account, "", 0, nullptr, SEC_PLAYER, EXPANSION_WRATH_OF_THE_LICH_KING, time_t(0),
                             LOCALE_enUS, 0, false, false, 0, true);
        Player* bot = factory.CreateRandomBot(&session, playerClass, names, raceMask);
        if (!bot && regional)
        {
            // This class cannot be one of the place's races: an ordinary newcomer instead.
            regional = nullptr;
            bot = factory.CreateRandomBot(&session, playerClass, names);
        }
        if (!bot)
            continue;
        Record record;
        record.account = account;
        record.race = bot->getRace();
        record.playerClass = playerClass;
        record.character.id = bot->GetGUID().GetCounter();
        record.character.faction = IsAlliance(record.race) ? 0 : 1;
        uint32_t hash = Policy::Mix(record.character.id);
        record.plannedLevel = 1 + hash % sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
        if (regional && regional->faction == record.character.faction)
        {
            record.plannedLevel = plannedLevel;
            record.demandHuman = regional->humans.empty() ? 0 : regional->humans.front().id;
            record.character.map = regional->map;
            record.character.zone = regional->zone;
        }
        if (playerClass == CLASS_DEATH_KNIGHT)
            record.plannedLevel = std::max(record.plannedLevel, sWorld->getIntConfig(CONFIG_START_HEROIC_PLAYER_LEVEL));
        record.character.level = record.plannedLevel;
        auto transaction = CharacterDatabase.BeginTransaction();
        bot->SaveToDB(transaction, true, false);
        std::string name = bot->GetName();
        uint8_t gender = bot->getGender(), level = bot->GetLevel();
        _accountClasses[account].insert(playerClass);
        ++_pendingCreations;
        _transactions.AddCallback(CharacterDatabase.AsyncCommitTransaction(transaction))
            .AfterComplete(
                [this, record, name, gender, level](bool success)
                {
                    --_pendingCreations;
                    if (!success)
                    {
                        _accountClasses[record.account].erase(record.playerClass);
                        LOG_ERROR("playerbots",
                                  "Population character creation transaction failed; no identity admitted");
                        return;
                    }
                    auto guid = ObjectGuid::Create<HighGuid::Player>(record.character.id);
                    sCharacterCache->AddCharacterCacheEntry(guid, record.account, name, gender, record.race,
                                                            record.playerClass, level);
                    _bots.emplace(record.character.id, record);
                    Save(record);
                });
        bot->CleanupsBeforeDelete();
        delete bot;
        ++created;
    }
    LOG_INFO("playerbots", "Population queued {} character creations; persistent roster {} / reserve target {}",
             created, _bots.size(), _settings.reserveTarget);
}
