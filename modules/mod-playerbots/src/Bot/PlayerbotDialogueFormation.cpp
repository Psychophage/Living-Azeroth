// SPDX-License-Identifier: GPL-2.0-or-later
// Living Azeroth changes; upstream authorship retained in AUTHORS.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <map>
#include <optional>
#include <sstream>
#include <tuple>

#include "Formations.h"
#include "Group.h"
#include "Map.h"
#include "Playerbots.h"

namespace
{
struct SpreadCache
{
    WorldLocation anchor;
    std::chrono::steady_clock::time_point expires;
    uint32 members = 0;
    std::map<ObjectGuid, WorldLocation> locations;
};
using SpreadKey = std::tuple<uint64, uint64, uint32, uint32, uint32>;
// Map threads never share raw objects or mutable geometry caches.
thread_local std::map<SpreadKey, SpreadCache> SpreadCaches;
thread_local bool BenchmarkUncached = false;
thread_local uint64 GeometryChecks = 0;
thread_local uint64 CacheHits = 0;

class DialogueSpreadFormation final : public MoveAheadFormation
{
public:
    DialogueSpreadFormation(PlayerbotAI* ai, bool hold)
        : MoveAheadFormation(ai, hold ? "spread_hold" : "spread"), _hold(hold)
    {
    }

    float GetMaxDistance() override { return 0.75f; }

    WorldLocation GetLocationInternal() override
    {
        auto master = GetMaster();
        Map* map = nullptr;
        if (!ValidateTargetContext(master, bot, map))
            return NullLocation;
        if (_held)
            return _held->GetMapId() == bot->GetMapId() ? *_held : NullLocation;
        auto group = master->GetGroup();
        SpreadKey key{master->GetGUID().GetRawValue(), group ? group->GetGUID().GetRawValue() : 0, master->GetMapId(),
                      master->GetInstanceId(), master->GetPhaseMask()};
        auto now = std::chrono::steady_clock::now();
        auto cached = SpreadCaches.find(key);
        auto memberCount = group ? group->GetMembersCount() : 1;
        if (!BenchmarkUncached && cached != SpreadCaches.end() && cached->second.expires > now &&
            cached->second.members == memberCount && master->GetExactDist(&cached->second.anchor) < 0.1f)
        {
            auto location = cached->second.locations.find(bot->GetGUID());
            if (location != cached->second.locations.end() &&
                master->IsWithinLOS(location->second.GetPositionX(), location->second.GetPositionY(),
                                    location->second.GetPositionZ()))
            {
                ++CacheHits;
                if (_hold)
                    _held = location->second;
                return location->second;
            }
        }
        std::vector<Player*> members;
        if (group)
            for (auto ref = group->GetFirstMember(); ref; ref = ref->next())
                if (auto member = ref->GetSource(); member && member != master && member->IsAlive() &&
                                                    member->IsInMap(master) && member->InSamePhase(master) &&
                                                    GET_PLAYERBOT_AI(member))
                    members.push_back(member);
        if (std::find(members.begin(), members.end(), bot) == members.end())
            members.push_back(bot);
        std::sort(
            members.begin(), members.end(),
            [&](auto first, auto second)
            {
                auto role = [&](auto member) { return botAI->IsTank(member) ? 0 : botAI->IsHeal(member) ? 1 : 2; };
                return std::tuple{role(first), first->GetGUID()} < std::tuple{role(second), second->GetGUID()};
            });
        auto index = static_cast<std::size_t>(std::find(members.begin(), members.end(), bot) - members.begin());
        SpreadCache generated;
        generated.anchor =
            WorldLocation(master->GetMapId(), master->GetPositionX(), master->GetPositionY(), master->GetPositionZ());
        generated.expires = now + std::chrono::milliseconds(250);
        generated.members = memberCount;
        std::size_t valid = 0;
        constexpr float separation = 6.0f;
        constexpr std::array<std::pair<int, int>, 6> directions = {
            {{0, -1}, {-1, 0}, {-1, 1}, {0, 1}, {1, 0}, {1, -1}}};
        // Every member enumerates the same bounded hexagonal cells. Reject wall-
        // clamped cells rather than assigning several characters the same corner.
        for (int ring = 1; ring <= 6; ++ring)
        {
            int q = ring, r = 0;
            for (auto const& [dq, dr] : directions)
                for (int step = 0; step < ring; ++step, q += dq, r += dr)
                {
                    float x = master->GetPositionX() + separation * (q + 0.5f * r);
                    float y = master->GetPositionY() + separation * 0.8660254f * r;
                    float z = master->GetPositionZ();
                    float expectedX = x, expectedY = y;
                    ++GeometryChecks;
                    if (!map->CheckCollisionAndGetValidCoords(master, master->GetPositionX(), master->GetPositionY(),
                                                              master->GetPositionZ(), x, y, z) ||
                        std::hypot(x - expectedX, y - expectedY) > 0.5f)
                        continue;
                    float ground = master->GetMapHeight(x, y, z + 2.0f);
                    if (ground <= INVALID_HEIGHT || std::abs(ground - master->GetPositionZ()) > 4.0f ||
                        !master->IsWithinLOS(x, y, ground))
                        continue;
                    WorldLocation location(master->GetMapId(), x, y, ground);
                    generated.locations.emplace(members[valid]->GetGUID(), location);
                    if (BenchmarkUncached && valid == index)
                        return location;
                    if (++valid == members.size())
                        return Store(key, std::move(generated));
                }
        }
        return Store(key, std::move(generated));
    }

private:
    WorldLocation Store(SpreadKey const& key, SpreadCache generated)
    {
        auto location = generated.locations.find(bot->GetGUID());
        WorldLocation result = location == generated.locations.end() ? NullLocation : location->second;
        if (!BenchmarkUncached)
        {
            if (SpreadCaches.size() >= 128 && !SpreadCaches.contains(key))
                SpreadCaches.clear();
            SpreadCaches.insert_or_assign(key, std::move(generated));
        }
        if (_hold && !IsNullLocation(result))
            _held = result;
        return result;
    }

    bool _hold;
    std::optional<WorldLocation> _held;
};
}  // namespace

Formation* CreateDialogueSpreadFormation(PlayerbotAI* botAI, bool hold)
{
    return new DialogueSpreadFormation(botAI, hold);
}

std::string BenchmarkDialogueSpreadFormation(Player* requester, unsigned rounds)
{
    if (!requester || !requester->GetGroup() || !rounds || rounds > 100)
        return "Formation benchmark needs a party and 1-100 rounds.";
    std::vector<PlayerbotAI*> bots;
    for (auto ref = requester->GetGroup()->GetFirstMember(); ref; ref = ref->next())
        if (auto member = ref->GetSource(); member && member != requester && member->IsInMap(requester))
            if (auto ai = GET_PLAYERBOT_AI(member))
                bots.push_back(ai);
    std::ostringstream output;
    output << "Formation benchmark bots=" << bots.size() << " rounds=" << rounds;
    for (bool uncached : {true, false})
    {
        BenchmarkUncached = uncached;
        GeometryChecks = CacheHits = 0;
        SpreadCaches.clear();
        auto started = std::chrono::steady_clock::now();
        unsigned valid = 0;
        for (unsigned round = 0; round < rounds; ++round)
            for (auto ai : bots)
            {
                DialogueSpreadFormation formation(ai, false);
                valid += !Formation::IsNullLocation(formation.GetLocation());
            }
        auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
        output << (uncached ? " uncached" : " cached") << "_us=" << elapsed << " geometry=" << GeometryChecks
               << " hits=" << CacheHits << " valid=" << valid;
    }
    BenchmarkUncached = false;
    return output.str();
}
