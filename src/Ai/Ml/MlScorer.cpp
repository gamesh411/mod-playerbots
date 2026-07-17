/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "MlScorer.h"

#include <algorithm>
#include <vector>

#include "Action.h"
#include "HeuristicScores.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "SharedDefines.h"

MlScorer& MlScorer::instance()
{
    static MlScorer inst;
    return inst;
}

void MlScorer::Reload()
{
    attemptedLoad = true;
    classModels.clear();
    fallbackModel = MlMlpModel{};

    auto loadClass = [&](uint8 playerClass, std::string const& path) {
        if (path.empty())
            return;
        MlMlpModel model;
        if (model.Load(path))
            classModels[playerClass] = std::move(model);
    };

    loadClass(CLASS_WARRIOR, sPlayerbotAIConfig.mlModelPathDuelWarrior);
    loadClass(CLASS_MAGE, sPlayerbotAIConfig.mlModelPathDuelMage);

    if (!sPlayerbotAIConfig.mlModelPathDuel.empty())
        fallbackModel.Load(sPlayerbotAIConfig.mlModelPathDuel);
}

bool MlScorer::ModelLoaded() const
{
    return fallbackModel.IsLoaded() || !classModels.empty();
}

bool MlScorer::HasModelFor(uint8 playerClass) const
{
    return ModelFor(playerClass) != nullptr;
}

MlMlpModel const* MlScorer::ModelFor(uint8 playerClass) const
{
    auto it = classModels.find(playerClass);
    if (it != classModels.end() && it->second.IsLoaded())
        return &it->second;
    if (fallbackModel.IsLoaded())
        return &fallbackModel;
    return nullptr;
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

float MlScorer::ScoreDuel(PlayerbotAI* botAI, Action* action, CombatFeatureVector const& features)
{
    if (!attemptedLoad)
        Reload();

    if (!action || !botAI || !botAI->GetBot())
        return 0.0f;

    MlMlpModel const* model = ModelFor(botAI->GetBot()->getClass());
    if (!model)
        return 0.0f;

    size_t dim = model->InputDim();
    if (dim != ML_INPUT_DIM && dim != ML_INPUT_DIM_V1)
        return 0.0f;

    std::vector<float> input(dim);
    BuildInputForDim(features, action->getName(), input.data(), dim);
    return model->Forward(input.data(), dim);
}
