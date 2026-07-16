/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "MlScorer.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "Action.h"
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

void MlScorer::BuildInput(CombatFeatureVector const& features, std::string const& actionName, float* out) const
{
    BuildInputForDim(features, actionName, out, ML_INPUT_DIM);
}

void MlScorer::BuildInputForDim(CombatFeatureVector const& features, std::string const& actionName, float* out,
                                size_t outDim) const
{
    float flags[AF_COUNT];
    HeuristicScores::FillActionFlags(actionName, flags);

    // Old 20-D PBML1: core[0..11] + action flags. New layout puts flags after all packs.
    if (outDim == ML_INPUT_DIM_V1)
    {
        for (size_t i = 0; i < 12; ++i)
            out[i] = features[i];
        for (size_t i = 0; i < AF_COUNT; ++i)
            out[12 + i] = flags[i];
        return;
    }

    for (size_t i = 0; i < outDim; ++i)
        out[i] = 0.0f;

    size_t nFeat = (std::min)(static_cast<size_t>(CF_FEATURE_COUNT), outDim);
    for (size_t i = 0; i < nFeat; ++i)
        out[i] = features[i];

    if (outDim >= CF_FEATURE_COUNT + AF_COUNT)
    {
        for (size_t i = 0; i < AF_COUNT; ++i)
            out[CF_FEATURE_COUNT + i] = flags[i];
    }
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

    size_t dim = hybridModel.InputDim();
    if (dim != ML_INPUT_DIM && dim != ML_INPUT_DIM_V1)
        return heuristic;

    std::vector<float> input(dim);
    BuildInputForDim(features, action->getName(), input.data(), dim);
    float raw = hybridModel.Forward(input.data(), dim);
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

    size_t dim = pvpModel.InputDim();
    if (dim != ML_INPUT_DIM && dim != ML_INPUT_DIM_V1)
        return heuristic;

    std::vector<float> input(dim);
    BuildInputForDim(features, action->getName(), input.data(), dim);
    float raw = pvpModel.Forward(input.data(), dim);
    float nn = RawToMultiplier(raw, 0.30f, 1.90f);
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    return (1.0f - alpha) * heuristic + alpha * nn;
}
