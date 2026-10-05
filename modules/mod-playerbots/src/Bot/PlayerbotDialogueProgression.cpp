/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: change a supported native tactic without altering talents or unlocking a spec.
#include "PlayerbotDialogueProgression.h"

#include <array>

#include "AiFactory.h"
#include "DBCStores.h"
#include "Playerbots.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Strategy.h"

namespace PlayerbotDialogue
{
namespace
{
uint32_t RoleType(uint32_t role)
{
    return role == 1 ? STRATEGY_TYPE_TANK : role == 2 ? STRATEGY_TYPE_HEAL : role == 3 ? STRATEGY_TYPE_DPS : 0;
}

std::string RoleStrategy(Player* bot, PlayerbotAI* ai, uint32_t role)
{
    std::vector<std::string> names;
    auto tab = AiFactory::GetPlayerSpecTab(bot);
    if (role == 1)
        names = {"tank", "bear", "blood"};
    else if (role == 2)
        names = {"heal", "resto", "holy heal"};
    else if (role == 3)
    {
        switch (bot->getClass())
        {
            case CLASS_WARRIOR:
                names = {tab == WARRIOR_TAB_FURY ? "fury" : "arms"};
                break;
            case CLASS_DRUID:
                names = {tab == DRUID_TAB_BALANCE ? "balance" : "cat"};
                break;
            case CLASS_SHAMAN:
                names = {tab == SHAMAN_TAB_ELEMENTAL ? "ele" : "enh"};
                break;
            case CLASS_DEATH_KNIGHT:
                names = {tab == DEATH_KNIGHT_TAB_UNHOLY ? "unholy" : "frost"};
                break;
            case CLASS_PRIEST:
            case CLASS_PALADIN:
                names = {"dps"};
                break;
            case CLASS_MAGE:
                names = {tab == MAGE_TAB_ARCANE ? "arcane" : tab == MAGE_TAB_FIRE ? "fire" : "frost"};
                break;
            case CLASS_HUNTER:
                names = {tab == HUNTER_TAB_BEAST_MASTERY ? "bm" : tab == HUNTER_TAB_MARKSMANSHIP ? "mm" : "surv"};
                break;
            case CLASS_ROGUE:
                names = {tab == ROGUE_TAB_COMBAT ? "combat" : "assassin"};
                break;
            case CLASS_WARLOCK:
                names = {tab == WARLOCK_TAB_AFFLICTION ? "affli" : tab == WARLOCK_TAB_DEMONOLOGY ? "demo" : "destro"};
                break;
            default:
                return {};
        }
    }
    for (auto const& name : names)
        if (auto strategy = ai->GetAiObjectContext()->GetStrategy(name);
            strategy && (strategy->GetType() & RoleType(role)))
            return name;
    return {};
}

std::string SpecTalents(Player* bot, uint32_t spec)
{
    // DBC talent-tab names are not loaded by the core; use the fixed client tree order.
    static std::map<uint8_t, std::array<std::string, 3>> const names = {
        {CLASS_WARRIOR, {"Arms", "Fury", "Protection"}},
        {CLASS_PALADIN, {"Holy", "Protection", "Retribution"}},
        {CLASS_HUNTER, {"Beast Mastery", "Marksmanship", "Survival"}},
        {CLASS_ROGUE, {"Assassination", "Combat", "Subtlety"}},
        {CLASS_PRIEST, {"Discipline", "Holy", "Shadow"}},
        {CLASS_DEATH_KNIGHT, {"Blood", "Frost", "Unholy"}},
        {CLASS_SHAMAN, {"Elemental", "Enhancement", "Restoration"}},
        {CLASS_MAGE, {"Arcane", "Fire", "Frost"}},
        {CLASS_WARLOCK, {"Affliction", "Demonology", "Destruction"}},
        {CLASS_DRUID, {"Balance", "Feral Combat", "Restoration"}}};
    auto found = names.find(bot->getClass());
    auto tabs = GetTalentTabPages(bot->getClass());
    if (found == names.end() || !tabs)
        return {};
    std::array<uint32_t, 3> points = {};
    for (auto const& [spell, talent] : bot->GetTalentMap())
        if (talent->State != PLAYERSPELL_REMOVED && talent->IsInSpec(spec))
            if (auto pos = GetTalentSpellPos(spell))
                if (auto entry = sTalentStore.LookupEntry(pos->talent_id))
                    for (size_t tab = 0; tab < points.size(); ++tab)
                        if (entry->TalentTab == tabs[tab])
                            points[tab] += pos->rank + 1;
    std::string description = "; allocated talents: ";
    for (size_t tab = 0; tab < points.size(); ++tab)
        description += (tab ? ", " : "") + found->second[tab] + " " + std::to_string(points[tab]);
    return description;
}

uint32_t SpecSpell(Player* bot, uint32_t spec)
{
    if (spec >= bot->GetSpecsCount())
        return 0;
    for (auto const& [id, known] : bot->GetSpellMap())
    {
        if (!bot->HasActiveSpell(id))
            continue;
        auto info = sSpellMgr->GetSpellInfo(id);
        if (!info || info->IsPassive())
            continue;
        for (auto const& effect : info->Effects)
            if (effect.Effect == SPELL_EFFECT_TALENT_SPEC_SELECT && effect.CalcValue(bot) == int32_t(spec + 1))
                return id;
    }
    return 0;
}
}  // namespace

std::map<uint32_t, std::string> AvailableRoles(Player* bot, PlayerbotAI* ai)
{
    std::map<uint32_t, std::string> roles;
    for (uint32_t role = 1; role <= 3; ++role)
        if (!RoleStrategy(bot, ai, role).empty())
            roles.emplace(role, role == 1 ? "tank" : role == 2 ? "healer" : "damage dealer");
    return roles;
}

Outcome ChangeRole(Player* bot, PlayerbotAI* ai, Request const& request, uint64_t nowMs)
{
    auto name = RoleStrategy(bot, ai, request.objectId);
    if (name.empty())
        return {request.id, Status::Failed, "native_tactical_role_unavailable", nowMs};
    auto strategy = ai->GetAiObjectContext()->GetStrategy(name);
    auto canonical = strategy->getName();
    ai->ChangeStrategy("+" + name, BOT_STATE_COMBAT);
    ai->ChangeStrategy(request.objectId == 1 ? "+tank assist,-dps assist" : "-tank assist,+dps assist",
                       BOT_STATE_COMBAT);
    bool changed = ai->HasStrategy(canonical, BOT_STATE_COMBAT) && (strategy->GetType() & RoleType(request.objectId));
    return {request.id, changed ? Status::Completed : Status::Failed,
            changed ? "native_tactical_role_changed:" + canonical : "native_tactical_role_not_observed", nowMs};
}

std::map<uint32_t, std::string> AvailableSpecs(Player* bot)
{
    std::map<uint32_t, std::string> result;
    for (uint32_t index = 0; index < bot->GetSpecsCount(); ++index)
        if (index == bot->GetActiveSpec() || SpecSpell(bot, index))
            result.emplace(index, std::string(index ? "Secondary" : "Primary") + " unlocked specialization" +
                                      (index == bot->GetActiveSpec() ? " (currently active)" : "") +
                                      SpecTalents(bot, index));
    return result;
}

Outcome StartSpec(Player* bot, SpecExecution& execution, uint64_t nowMs)
{
    auto const& request = execution.request;
    if (request.objectId >= bot->GetSpecsCount() || bot->IsInCombat() || bot->GetTradeData() ||
        bot->IsNonMeleeSpellCast(false) || nowMs >= request.deadlineMs)
        return {request.id, Status::Failed, "native_specialization_unavailable", nowMs};
    if (request.objectId == bot->GetActiveSpec())
        return {request.id, Status::Completed, "requested_specialization_already_active", nowMs};
    execution.spell = SpecSpell(bot, request.objectId);
    if (!execution.spell)
        return {request.id, Status::Failed, "known_specialization_switch_spell_missing", nowMs};
    bot->StopMoving();
    execution.castAtMs = nowMs;
    // Use the ordinary non-triggered switch spell. This preserves cast time,
    // interruption, battleground restrictions and native selection hooks.
    if (bot->CastSpell(bot, execution.spell, false) != SPELL_CAST_OK)
        return {request.id, Status::Failed, "native_specialization_cast_refused", nowMs};
    return {request.id, Status::Started, "native_specialization_cast_started", nowMs};
}

Outcome PollSpec(Player* bot, SpecExecution const& execution, uint64_t nowMs)
{
    auto const& request = execution.request;
    if (request.objectId < bot->GetSpecsCount() && request.objectId == bot->GetActiveSpec())
        return {request.id, Status::Completed, "native_specialization_switch_observed", nowMs};
    bool casting = false;
    for (auto slot : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL})
        if (auto cast = bot->GetCurrentSpell(slot); cast && cast->m_spellInfo->Id == execution.spell)
            casting = true;
    if (nowMs >= request.deadlineMs || (nowMs > execution.castAtMs + 1000 && !casting))
        return {request.id, Status::Failed, "native_specialization_switch_interrupted", nowMs};
    return {request.id, Status::Pending, "awaiting_native_specialization", nowMs};
}

void CancelSpec(Player* bot, SpecExecution const& execution)
{
    for (auto slot : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL})
        if (auto cast = bot->GetCurrentSpell(slot); cast && cast->m_spellInfo->Id == execution.spell)
            bot->InterruptSpell(slot, false);
}
}  // namespace PlayerbotDialogue
