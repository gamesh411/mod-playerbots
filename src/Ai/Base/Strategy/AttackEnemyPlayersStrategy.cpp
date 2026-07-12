/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AttackEnemyPlayersStrategy.h"

#include "Playerbots.h"

void AttackEnemyPlayersStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Slightly higher than historical 55 so hybrid/pvp multipliers can still rank around it.
    triggers.push_back(new TriggerNode("enemy player near",
                                       { NextAction("attack enemy player", 58.0f) }));
}
