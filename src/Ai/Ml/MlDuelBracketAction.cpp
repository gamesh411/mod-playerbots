/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "MlDuelBracketAction.h"

#include "MlDuelBracket.h"
#include "Playerbots.h"

bool MlDuelBracketAction::isUseful()
{
    return sMlDuelBracket.IsEnabled();
}

bool MlDuelBracketAction::Execute(Event /*event*/)
{
    return sMlDuelBracket.TryMatchOrQueue(botAI);
}
