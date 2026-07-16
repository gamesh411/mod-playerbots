/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AcceptDuelAction.h"

#include "Event.h"
#include "MlDuelBracket.h"
#include "Playerbots.h"

bool AcceptDuelAction::Execute(Event event)
{
    WorldPacket p(event.getPacket());

    ObjectGuid flagGuid;
    p >> flagGuid;
    ObjectGuid playerGuid;
    p >> playerGuid;

    bool const skipResourceGate =
        botAI->HasRealPlayerMaster() && botAI->GetMaster() && botAI->GetMaster()->GetGUID() == playerGuid;
    bool refuse = false;
    if (!skipResourceGate)
    {
        if (sMlDuelBracket.IsEnabled())
        {
            // DEC-023/024: same hard gate as IsIdleEligible (restore then check).
            sMlDuelBracket.RestoreForRematch(bot);
            refuse = !sMlDuelBracket.IsResourceReady(bot);
        }
        else
            refuse = AI_VALUE2(uint8, "health", "self target") < 90;
    }

    if (refuse)
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
