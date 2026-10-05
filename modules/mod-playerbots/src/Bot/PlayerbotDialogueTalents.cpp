/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: project configured builds before spending; native LearnTalent enforces game rules.
#include "PlayerbotDialogueTalents.h"

#include <functional>
#include <set>

#include "DBCStores.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "SpellMgr.h"

namespace PlayerbotDialogue
{
std::vector<TalentRank> TalentPlan(Player* bot, uint32_t preset)
{
    std::vector<TalentRank> result;
    auto cls = bot->getClass();
    uint32_t remaining = bot->CalculateTalentsPoints();
    if (preset >= MAX_SPECNO || sPlayerbotAIConfig.premadeSpecName[cls][preset].empty() || !remaining)
        return {};
    // Match native preset progression: nearest lower configured level, then later
    // builds in order, cropped to this character's actual realm talent allowance.
    uint32_t start = std::min<uint32_t>(bot->GetLevel(), 80);
    while (start > 1 && sPlayerbotAIConfig.parsedSpecLinkOrder[cls][preset][start].empty())
        --start;
    std::map<uint32_t, uint32_t> learned, treePoints;
    std::set<uint32_t> visiting;
    std::function<bool(TalentEntry const*, uint32_t)> add = [&](TalentEntry const* talent, uint32_t count)
    {
        if (!talent || !count || count > MAX_TALENT_RANK)
            return false;
        auto tab = sTalentTabStore.LookupEntry(talent->TalentTab);
        if (!tab || !(tab->ClassMask & bot->getClassMask()) || !visiting.insert(talent->TalentID).second)
            return false;
        if (talent->DependsOn && learned[talent->DependsOn] <= talent->DependsOnRank)
            if (!add(sTalentStore.LookupEntry(talent->DependsOn), talent->DependsOnRank + 1))
                return false;
        visiting.erase(talent->TalentID);
        auto& current = learned[talent->TalentID];
        uint32_t next = std::min(count, current + remaining);
        if (next <= current)
            return true;
        if (treePoints[talent->TalentTab] < talent->Row * MAX_TALENT_RANK || !talent->RankID[next - 1] ||
            !sSpellMgr->GetSpellInfo(talent->RankID[next - 1]))
            return false;
        remaining -= next - current;
        treePoints[talent->TalentTab] += next - current;
        current = next;
        result.push_back({talent->TalentID, next - 1});
        return true;
    };
    for (uint32_t level = start; level <= 80 && remaining; ++level)
        for (auto const& desired : sPlayerbotAIConfig.parsedSpecLinkOrder[cls][preset][level])
        {
            if (!remaining)
                break;
            if (desired.size() != 4)
                return {};
            TalentEntry const* match = nullptr;
            for (uint32_t row = 0; row < sTalentStore.GetNumRows(); ++row)
                if (auto talent = sTalentStore.LookupEntry(row);
                    talent && talent->Row == desired[1] && talent->Col == desired[2])
                    if (auto tab = sTalentTabStore.LookupEntry(talent->TalentTab);
                        tab && (tab->ClassMask & bot->getClassMask()) && tab->tabpage == desired[0])
                    {
                        if (match)
                            return {};
                        match = talent;
                    }
            if (!add(match, desired[3]))
                return {};
        }
    // Incomplete/invalid presets are not offered as full rebuilds.
    return remaining ? std::vector<TalentRank>{} : result;
}

std::map<uint32_t, std::string> AvailableTalentPresets(Player* bot)
{
    std::map<uint32_t, std::string> result;
    for (uint32_t preset = 0; preset < MAX_SPECNO; ++preset)
        if (!TalentPlan(bot, preset).empty())
            result.emplace(preset, sPlayerbotAIConfig.premadeSpecName[bot->getClass()][preset] + " (" +
                                       std::to_string(bot->CalculateTalentsPoints()) + " points, active spec only)");
    return result;
}

bool HasAllocatedTalents(Player* bot)
{
    for (auto const& [spell, talent] : bot->GetTalentMap())
        if (talent->State != PLAYERSPELL_REMOVED && talent->IsInSpec(bot->GetActiveSpec()))
            return true;
    return false;
}

bool ApplyTalentPlan(Player* bot, Request const& request)
{
    if (request.talentPlan.empty() || request.talentSpec != bot->GetActiveSpec())
        return false;
    for (auto const& desired : request.talentPlan)
    {
        bot->LearnTalent(desired.talent, desired.rank);  // No command/cheat flag.
        auto talent = sTalentStore.LookupEntry(desired.talent);
        if (!talent || !bot->HasTalent(talent->RankID[desired.rank], bot->GetActiveSpec()))
            return false;
    }
    bot->SendTalentsInfoData(false);
    return bot->GetFreeTalentPoints() == 0;
}
}  // namespace PlayerbotDialogue
