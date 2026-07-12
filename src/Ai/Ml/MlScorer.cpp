/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "MlScorer.h"

#include <algorithm>
#include <cmath>

#include "HeuristicScores.h"
#include "PlayerbotAIConfig.h"

MlScorer& MlScorer::instance()
{
    static MlScorer inst;
    return inst;
}

void MlScorer::Reload()
{
    attemptedLoad = true;
    if (!sPlayerbotAIConfig.mlModelPathHybrid.empty())
        hybridModel.Load(sPlayerbotAIConfig.mlModelPathHybrid);
    if (!sPlayerbotAIConfig.mlModelPathPvp.empty())
        pvpModel.Load(sPlayerbotAIConfig.mlModelPathPvp);
}

void MlScorer::BuildInput(CombatFeatureVector const& features, std::string const& actionName, float* out18) const
{
    for (size_t i = 0; i < CF_FEATURE_COUNT; ++i)
        out18[i] = features[i];

    float flags[AF_COUNT];
    HeuristicScores::FillActionFlags(actionName, flags);
    for (size_t i = 0; i < AF_COUNT; ++i)
        out18[CF_FEATURE_COUNT + i] = flags[i];
}

float MlScorer::RawToMultiplier(float raw, float lo, float hi) const
{
    // Squash unbounded regression output into multiplier range via sigmoid around 1.0
    float sig = 1.0f / (1.0f + std::exp(-raw));
    float mid = 0.5f * (lo + hi);
    float half = 0.5f * (hi - lo);
    float m = mid + (2.0f * sig - 1.0f) * half;
    return std::clamp(m, lo, hi);
}

float MlScorer::ScoreHybrid(PlayerbotAI* botAI, Action* action, CombatFeatureVector const& features)
{
    float heuristic = HeuristicScores::Hybrid(botAI, action, features);
    if (!attemptedLoad)
        Reload();

    float alpha = sPlayerbotAIConfig.mlHybridAlpha;
    if (alpha <= 0.0f || !hybridModel.IsLoaded() || !action)
        return heuristic;

    float input[ML_INPUT_DIM];
    BuildInput(features, action->getName(), input);
    float raw = hybridModel.Forward(input, ML_INPUT_DIM);
    float nn = RawToMultiplier(raw, 0.35f, 1.75f);
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    return (1.0f - alpha) * heuristic + alpha * nn;
}

float MlScorer::ScorePvp(PlayerbotAI* botAI, Action* action, CombatFeatureVector const& features)
{
    float heuristic = HeuristicScores::PvpPolicy(botAI, action, features);
    if (!attemptedLoad)
        Reload();

    float alpha = sPlayerbotAIConfig.mlPvpAlpha;
    if (alpha <= 0.0f || !pvpModel.IsLoaded() || !action)
        return heuristic;

    float input[ML_INPUT_DIM];
    BuildInput(features, action->getName(), input);
    float raw = pvpModel.Forward(input, ML_INPUT_DIM);
    float nn = RawToMultiplier(raw, 0.30f, 1.90f);
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    return (1.0f - alpha) * heuristic + alpha * nn;
}
