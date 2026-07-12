/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "AttackEnemyPlayersStrategy.h"

#include "Playerbots.h"

void AttackEnemyPlayersStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Slightly higher than historical 55 so hybrid/pvp multipliers can still rank around it.
    triggers.push_back(new TriggerNode("enemy player near",
                                       { NextAction("attack enemy player", 58.0f) }));
}
