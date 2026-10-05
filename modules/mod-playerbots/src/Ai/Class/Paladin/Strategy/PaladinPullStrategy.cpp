// PBC Character System integration changes, 2026-09-30; upstream notices preserved.
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PaladinPullStrategy.h"

#include "PlayerbotAI.h"
#include "Playerbots.h"

std::string PaladinPullStrategy::GetPullActionName() const
{
    Unit* target = GetTarget();
    bool tank = botAI->HasStrategy("tank", BOT_STATE_COMBAT) || botAI->HasStrategy("tank", BOT_STATE_NON_COMBAT);
    if (target && tank)
    {
        if (botAI->CanCastSpell("avenger's shield", target))
            return "avenger's shield";
        if (botAI->CanCastSpell("hand of reckoning", target))
            return "hand of reckoning";
    }

    // Living Azeroth: Wrath has named judgements, not a spell named "judgement".
    // Resolve a known native action instead of repeatedly trying spell ID zero.
    for (auto name : {"judgement of light", "judgement of wisdom", "judgement of justice", "judgement"})
        if (botAI->GetAiObjectContext()->GetValue<uint32>("spell id", name)->Get())
            return name;
    return PullStrategy::GetPullActionName();
}

std::string PaladinPullStrategy::GetPreActionName() const
{
    // A tank using a judgement fallback needs its seal just as a damage dealer does.
    if (GetPullActionName().starts_with("judgement"))
        return botAI->HasAura("seal of righteousness", botAI->GetBot()) ? "" : PullStrategy::GetPreActionName();
    if (botAI->HasStrategy("tank", BOT_STATE_COMBAT) || botAI->HasStrategy("tank", BOT_STATE_NON_COMBAT))
        return "";
    return PullStrategy::GetPreActionName();
}
