/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "PvpPolicyStrategy.h"

#include "CombatDecisionFeatures.h"
#include "MlScorer.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

float PvpPolicyMultiplier::GetValue(Action* action)
{
    if (!sPlayerbotAIConfig.pvpPolicyEnabled || !action)
        return 1.0f;

    Player* bot = botAI->GetBot();
    if (!bot || !(bot->InBattleground() || bot->InArena()))
        return 1.0f;

    CombatFeatureVector const features = AI_VALUE(CombatFeatureVector, "combat decision features");
    return sMlScorer.ScorePvp(botAI, action, features);
}

void PvpPolicyStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new PvpPolicyMultiplier(botAI));
}

void PvpPolicyStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("enemy player near", { NextAction("attack enemy player", ACTION_HIGH + 8.0f) }));
    triggers.push_back(
        new TriggerNode("enemy flagcarrier near", { NextAction("attack enemy flag carrier", ACTION_RAID + 2.0f) }));
}
