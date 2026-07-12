/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "HybridRelevanceStrategy.h"

#include "CombatDecisionFeatures.h"
#include "MlScorer.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

float HybridRelevanceMultiplier::GetValue(Action* action)
{
    if (!sPlayerbotAIConfig.hybridRelevanceEnabled || !action)
        return 1.0f;

    CombatFeatureVector const features = AI_VALUE(CombatFeatureVector, "combat decision features");
    return sMlScorer.ScoreHybrid(botAI, action, features);
}

void HybridRelevanceStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new HybridRelevanceMultiplier(botAI));
}
