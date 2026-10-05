// PBC Character System integration changes, 2026-09-30; upstream notices preserved.
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AcceptDuelAction.h"
#include "Event.h"
#include "Playerbots.h"

bool AcceptDuelAction::Execute(Event event)
{
    // Living Azeroth: the initiator also receives the request packet. It cannot
    // accept its own challenge or reset standing strategies before consent.
    if (!bot->duel || bot->duel->Initiator == bot)
        return false;
    WorldPacket p(event.getPacket());

    ObjectGuid flagGuid;
    p >> flagGuid;
    ObjectGuid playerGuid;
    p >> playerGuid;

    // Do not auto duel with low health
    if ((!botAI->HasGameClientMaster() ||
        (botAI->GetMaster() && botAI->GetMaster()->GetGUID() != playerGuid)) &&
        AI_VALUE2(uint8, "health", "self target") < 90)
    {
        WorldPacket packet(CMSG_DUEL_CANCELLED, 8);
        packet << flagGuid;
        bot->GetSession()->HandleDuelCancelledOpcode(packet);
        return false;
    }

    WorldPacket packet(CMSG_DUEL_ACCEPTED, 8);
    packet << flagGuid;
    bot->GetSession()->HandleDuelAcceptedOpcode(packet);

    botAI->ResetStrategies();
    return true;
}
