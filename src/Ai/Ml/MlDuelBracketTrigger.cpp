/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "MlDuelBracketTrigger.h"

#include "MlDuelBracket.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"

bool MlDuelBracketTrigger::IsActive()
{
    if (!sMlDuelBracket.IsEnabled())
        return false;

    Player* bot = botAI->GetBot();
    if (!bot)
        return false;

    return sMlDuelBracket.IsBotEligibleSpec(bot) && !bot->duel && !bot->InArena() && !bot->InBattleground();
}
