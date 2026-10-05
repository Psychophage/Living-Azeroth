/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
// Living Azeroth: effects are attributed using authoritative native packets and fresh game state.
#include "PlayerbotDialogueSupport.h"

#include <map>
#include <mutex>
#include <vector>

#include "CharmInfo.h"
#include "MovementActions.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "PlayerbotDialogue.h"
#include "Playerbots.h"
#include "ServerFacade.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldPacket.h"

namespace PlayerbotDialogue
{
namespace
{
struct EffectWatch
{
    std::string requestId;
    uint64_t target = 0;
    uint32_t spell = 0;
    uint32_t healed = 0;
    bool dispelled = false;
    bool resurrectionOffered = false;
    bool resurrectionAccepted = false;
};
std::mutex EffectMutex;
std::map<uint64_t, EffectWatch> Effects;

bool SupportTarget(Player* bot, Unit* target, Request const& request)
{
    return PlayerbotDialogueBridge::Matches(target, request.target) && bot->IsInMap(target) &&
           bot->InSamePhase(target) && bot->CanSeeOrDetect(target) && bot->IsFriendlyTo(target) &&
           bot->IsWithinDistInMap(target, 60.0f);
}

class ReachSupport final : public MovementAction
{
public:
    explicit ReachSupport(PlayerbotAI* ai) : MovementAction(ai, "dialogue support range") {}
    bool Reach(Unit* target, float range) { return IsMovingAllowed(target) && MoveNear(target, range); }
};

bool Useful(SpellInfo const* spell, Player* bot, Unit* target, Kind kind)
{
    if (!spell || !bot->HasSpell(spell->Id) || spell->IsPassive())
        return false;
    auto hasEffect = [&](uint32_t type)
    {
        return std::any_of(std::begin(spell->Effects), std::end(spell->Effects),
                           [&](auto const& effect) { return effect.Effect == type; });
    };
    if (kind == Kind::Heal)
    {
        bool periodic = std::any_of(std::begin(spell->Effects), std::end(spell->Effects), [](auto const& effect)
                                    { return effect.ApplyAuraName == SPELL_AURA_PERIODIC_HEAL; });
        return (hasEffect(SPELL_EFFECT_HEAL) || (periodic && !target->HasAura(spell->Id))) && target->IsAlive() &&
               target->GetHealth() < target->GetMaxHealth();
    }
    if (kind == Kind::Buff)
        return spell->IsPositive() &&
               (hasEffect(SPELL_EFFECT_APPLY_AURA) || hasEffect(SPELL_EFFECT_APPLY_AREA_AURA_PARTY) ||
                hasEffect(SPELL_EFFECT_APPLY_AREA_AURA_RAID)) &&
               target->IsAlive() && !target->HasAura(spell->Id);
    if (kind == Kind::Resurrect)
        return (hasEffect(SPELL_EFFECT_RESURRECT) || hasEffect(SPELL_EFFECT_RESURRECT_NEW)) && target->IsPlayer() &&
               !target->IsAlive();
    for (auto const& effect : spell->Effects)
        if (effect.Effect == SPELL_EFFECT_DISPEL)
        {
            DispelChargesList list;
            target->GetDispellableAuraList(bot, SpellInfo::GetDispelMask(DispelType(effect.MiscValue)), list, spell);
            if (!list.empty())
                return true;
        }
    return false;
}

std::vector<std::string> SpellNames(Player* bot, Unit* target, Kind kind)
{
    if (kind == Kind::Heal)
        switch (bot->getClass())
        {
            case CLASS_PRIEST:
                return {"flash heal", "heal", "lesser heal", "greater heal", "renew"};
            case CLASS_DRUID:
                return {"nourish", "healing touch", "regrowth", "rejuvenation", "lifebloom"};
            case CLASS_PALADIN:
                return {"flash of light", "holy light"};
            case CLASS_SHAMAN:
                return {"lesser healing wave", "healing wave"};
            default:
                return {"gift of the naaru"};
        }
    if (kind == Kind::Buff)
        switch (bot->getClass())
        {
            case CLASS_PRIEST:
                return {"power word: fortitude", "divine spirit", "shadow protection"};
            case CLASS_MAGE:
                return {"arcane intellect", "dampen magic"};
            case CLASS_DRUID:
                return {"mark of the wild", "thorns"};
            case CLASS_PALADIN:
                return target->getPowerType() == POWER_MANA
                           ? std::vector<std::string>{"blessing of kings", "blessing of wisdom", "blessing of might"}
                           : std::vector<std::string>{"blessing of kings", "blessing of might"};
            case CLASS_SHAMAN:
                return {"earth shield", "water shield", "lightning shield"};
            case CLASS_WARRIOR:
                return {"battle shout", "commanding shout"};
            case CLASS_WARLOCK:
                return {"unending breath", "detect invisibility"};
            default:
                return {};
        }
    if (kind == Kind::Cleanse)
        switch (bot->getClass())
        {
            case CLASS_PRIEST:
                return {"cure disease", "dispel magic", "abolish disease"};
            case CLASS_PALADIN:
                return {"cleanse", "purify"};
            case CLASS_DRUID:
                return {"remove curse", "cure poison", "abolish poison"};
            case CLASS_SHAMAN:
                return {"cleanse spirit", "cure toxins", "cure poison", "cure disease"};
            default:
                return {};
        }
    switch (bot->getClass())
    {
        case CLASS_PRIEST:
            return {"resurrection"};
        case CLASS_PALADIN:
            return {"redemption"};
        case CLASS_SHAMAN:
            return {"ancestral spirit"};
        case CLASS_DRUID:
            return {"revive", "rebirth"};
        default:
            return {};
    }
}

Outcome Progress(Player* bot, PlayerbotAI* ai, CombatExecution& execution, uint64_t nowMs)
{
    auto const& request = execution.request;
    auto result = [&](Status status, std::string detail)
    { return Outcome{request.id, status, std::move(detail), nowMs}; };
    auto target = ObjectAccessor::GetUnit(*bot, ObjectGuid(request.target.guid));
    if (!SupportTarget(bot, target, request))
        return result(Status::Failed, "bound_support_target_lost");
    if (nowMs >= request.deadlineMs || nowMs >= execution.startedMs + 30000)
        return result(Status::Failed, "native_support_effect_not_observed");
    auto spell = sSpellMgr->GetSpellInfo(execution.spellId);
    if (!spell)
        return result(Status::Failed, "support_spell_unavailable");
    if (execution.castStarted)
    {
        EffectWatch watch;
        {
            std::lock_guard lock(EffectMutex);
            auto found = Effects.find(bot->GetGUID().GetRawValue());
            if (found == Effects.end() || found->second.requestId != request.id)
                return result(Status::Failed, "support_tracking_lost");
            watch = found->second;
        }
        bool effect = request.kind == Kind::Heal      ? watch.healed > 0
                      : request.kind == Kind::Buff    ? target->GetAura(execution.spellId, bot->GetGUID()) != nullptr
                      : request.kind == Kind::Cleanse ? watch.dispelled
                                                      : watch.resurrectionAccepted && target->IsAlive();
        if (effect)
            return result(Status::Completed,
                          "native_support_effect_observed:spell=" + std::to_string(execution.spellId));
        // Resurrection acceptance belongs to the recipient, not the caster.
        if (request.kind == Kind::Resurrect && watch.resurrectionOffered)
            return result(Status::Pending, "awaiting_native_resurrection_acceptance");
        if (request.kind == Kind::Heal && target->GetAura(execution.spellId, bot->GetGUID()))
            return result(Status::Pending, "awaiting_native_healing_tick");
        auto casting = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!casting && nowMs > execution.castAtMs + spell->CalcCastTime(bot) + 3000)
            return result(Status::Failed, "cast_finished_without_requested_effect");
        return result(Status::Pending, "awaiting_native_support_effect");
    }
    if (!Useful(spell, bot, target, request.kind))
        return result(Status::Failed, "support_effect_no_longer_needed");
    float range = spell->GetMaxRange(true, bot, nullptr);
    if (range > 0 && !bot->IsWithinDistInMap(target, range - 1.0f))
    {
        ReachSupport(ai).Reach(target, std::max(1.0f, range - 3.0f));
        return result(Status::Pending, "approaching_native_spell_range");
    }
    if (bot->IsNonMeleeSpellCast(false))
        return result(Status::Pending, "waiting_for_current_cast");
    if (bot->GetGlobalCooldownMgr().HasGlobalCooldown(spell))
        return result(Status::Pending, "waiting_for_native_global_cooldown");
    if (spell->CalcCastTime(bot) && bot->isMoving())
        bot->StopMoving();
    ServerFacade::instance().SetFacingTo(bot, target);
    if (!ai->CanCastSpell(execution.spellId, target))
        return result(Status::Failed, "native_cast_unavailable");
    {
        std::lock_guard lock(EffectMutex);
        Effects[bot->GetGUID().GetRawValue()] = {request.id, target->GetGUID().GetRawValue(), execution.spellId};
    }
    execution.castStarted = true;
    execution.castAtMs = nowMs;
    if (!ai->CastSpell(execution.spellId, target))
        return result(Status::Failed, "native_cast_refused");
    return result(Status::Pending, "native_cast_started");
}
}  // namespace

bool HasUsefulSupport(Player* bot, PlayerbotAI* ai, Unit* target, Kind kind)
{
    if (!bot || !ai || !target || !bot->IsFriendlyTo(target))
        return false;
    for (auto const& name : SpellNames(bot, target, kind))
        if (Useful(sSpellMgr->GetSpellInfo(ai->GetAiObjectContext()->GetValue<uint32>("spell id", name)->Get()), bot,
                   target, kind))
            return true;
    return false;
}

Outcome StartSupport(Player* bot, PlayerbotAI* ai, CombatExecution& execution, uint64_t nowMs)
{
    auto const& request = execution.request;
    auto target = ObjectAccessor::GetUnit(*bot, ObjectGuid(request.target.guid));
    if (!SupportTarget(bot, target, request))
        return {request.id, Status::Failed, "bound_support_target_unavailable", nowMs};
    if (request.objectId)
    {
        if (Useful(sSpellMgr->GetSpellInfo(request.objectId), bot, target, request.kind))
            execution.spellId = request.objectId;
    }
    else
        for (auto const& name : SpellNames(bot, target, request.kind))
        {
            auto id = ai->GetAiObjectContext()->GetValue<uint32>("spell id", name)->Get();
            if (Useful(sSpellMgr->GetSpellInfo(id), bot, target, request.kind))
            {
                execution.spellId = id;
                break;
            }
        }
    if (!execution.spellId)
        return {request.id, Status::Failed, "no_known_useful_support_spell", nowMs};
    auto outcome = Progress(bot, ai, execution, nowMs);
    if (outcome.status == Status::Pending)
        outcome.status = Status::Started;
    return outcome;
}

Outcome PollSupport(Player* bot, PlayerbotAI* ai, CombatExecution& execution, uint64_t nowMs)
{
    auto outcome = Progress(bot, ai, execution, nowMs);
    if (outcome.status == Status::Completed)
    {
        std::lock_guard lock(EffectMutex);
        auto found = Effects.find(execution.request.actor.guid);
        if (found != Effects.end() && found->second.requestId == execution.request.id)
            Effects.erase(found);
    }
    return outcome;
}

void CancelSupport(PlayerbotAI* ai, Request const& request)
{
    uint32_t spellId = 0;
    {
        std::lock_guard lock(EffectMutex);
        auto found = Effects.find(request.actor.guid);
        if (found == Effects.end() || found->second.requestId != request.id)
            return;
        spellId = found->second.spell;
        Effects.erase(found);
    }
    auto bot = ai->GetBot();
    if (auto spell = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL); spell && spell->m_spellInfo->Id == spellId)
        bot->InterruptSpell(CURRENT_GENERIC_SPELL, false);
}

void ObserveSupportPacket(Player* receiver, WorldPacket const* packet)
{
    if (!receiver || !packet || !PlayerbotDialogueBridge::Enabled())
        return;
    auto opcode = packet->GetOpcode();
    if (opcode != SMSG_SPELLHEALLOG && opcode != SMSG_PERIODICAURALOG && opcode != SMSG_SPELLDISPELLOG &&
        opcode != SMSG_RESURRECT_REQUEST)
        return;
    try
    {
        WorldPacket data(*packet);
        data.rpos(0);
        uint64_t target = 0, caster = 0;
        uint32_t spell = 0, healed = 0;
        bool dispelled = false, resurrect = opcode == SMSG_RESURRECT_REQUEST;
        if (resurrect)
        {
            data >> caster;
            target = receiver->GetGUID().GetRawValue();
        }
        else
        {
            data.readPackGUID(target);
            data.readPackGUID(caster);
            data >> spell;
            // The caster receives these packets once; ignore copies sent to witnesses.
            if (receiver->GetGUID().GetRawValue() != caster)
                return;
            if (opcode == SMSG_PERIODICAURALOG)
            {
                uint32 count, aura, total, overheal;
                data >> count >> aura;
                if (count != 1 || (aura != SPELL_AURA_PERIODIC_HEAL && aura != SPELL_AURA_OBS_MOD_HEALTH))
                    return;
                data >> total >> overheal;
                healed = total > overheal ? total - overheal : 0;
            }
            else if (opcode == SMSG_SPELLHEALLOG)
            {
                uint32_t total, overheal;
                data >> total >> overheal;
                healed = total > overheal ? total - overheal : 0;
            }
            else
            {
                uint8_t unused;
                uint32_t count;
                data >> unused >> count;
                dispelled = count > 0;
            }
        }
        std::lock_guard lock(EffectMutex);
        auto found = Effects.find(caster);
        if (found != Effects.end() && found->second.target == target && (resurrect || found->second.spell == spell))
        {
            found->second.healed += healed;
            found->second.dispelled |= dispelled;
            found->second.resurrectionOffered |= resurrect;
        }
    }
    catch (ByteBufferException const&)
    {
        // A malformed native packet cannot establish an action outcome.
    }
}

void ObserveResurrection(Player* player, float restoredFraction)
{
    // Native acceptance uses the bound resurrection request (and zero here),
    // while healer/GM revivals have no such caster consent. A later resurrection
    // offered by somebody else must not complete this character's action.
    if (!player || restoredFraction != 0.0f || !PlayerbotDialogueBridge::Enabled())
        return;
    std::lock_guard lock(EffectMutex);
    for (auto& [caster, watch] : Effects)
        if (watch.target == player->GetGUID().GetRawValue() && watch.resurrectionOffered &&
            player->isResurrectRequestedBy(ObjectGuid(caster)))
            watch.resurrectionAccepted = true;
}
}  // namespace PlayerbotDialogue
