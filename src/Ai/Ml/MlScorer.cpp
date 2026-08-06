/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "MlScorer.h"

#include <algorithm>
#include <limits>
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
    teacherModels.clear();
    movementModels.clear();
    fallbackModel = MlMlpModel{};

    auto loadClass = [&](uint8 playerClass, std::string const& path, std::unordered_map<uint8, MlMlpModel>& into) {
        if (path.empty())
            return;
        MlMlpModel model;
        if (model.Load(path))
            into[playerClass] = std::move(model);
    };

    loadClass(CLASS_WARRIOR, sPlayerbotAIConfig.mlModelPathDuelWarrior, classModels);
    loadClass(CLASS_MAGE, sPlayerbotAIConfig.mlModelPathDuelMage, classModels);
    loadClass(CLASS_WARRIOR, sPlayerbotAIConfig.mlModelPathDuelTeacherWarrior, teacherModels);
    loadClass(CLASS_MAGE, sPlayerbotAIConfig.mlModelPathDuelTeacherMage, teacherModels);
    loadClass(CLASS_WARRIOR, sPlayerbotAIConfig.mlModelPathDuelMovementWarrior, movementModels);
    loadClass(CLASS_MAGE, sPlayerbotAIConfig.mlModelPathDuelMovementMage, movementModels);

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

MlMlpModel const* MlScorer::TeacherFor(uint8 playerClass) const
{
    auto it = teacherModels.find(playerClass);
    if (it != teacherModels.end() && it->second.IsLoaded())
        return &it->second;
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

    // Ability heads consume only the 70-feature slice; CF_MOVE never leaks in (DEC-036).
    size_t nFeat = (std::min)(static_cast<size_t>(CF_ABILITY_FEATURE_COUNT), outDim);
    for (size_t i = 0; i < nFeat; ++i)
        out[i] = features[i];

    if (outDim >= CF_ABILITY_FEATURE_COUNT + AF_COUNT)
    {
        for (size_t i = 0; i < AF_COUNT; ++i)
            out[CF_ABILITY_FEATURE_COUNT + i] = flags[i];
    }

    if (outDim >= CF_ABILITY_FEATURE_COUNT + AF_COUNT + AF_ID_COUNT)
    {
        float actionId[AF_ID_COUNT];
        HeuristicScores::FillActionIdFeatures(actionName, actionId);
        for (size_t i = 0; i < AF_ID_COUNT; ++i)
            out[CF_ABILITY_FEATURE_COUNT + AF_COUNT + i] = actionId[i];
    }
}

float MlScorer::ScoreWithModel(MlMlpModel const* model, Action* action, CombatFeatureVector const& features) const
{
    if (!model || !action)
        return 0.0f;

    size_t dim = model->InputDim();
    if (dim != ML_INPUT_DIM && dim != ML_INPUT_DIM_NO_ACTION_ID && dim != ML_INPUT_DIM_V1)
        return 0.0f;
    if (model->IsMultiLogit())
        return 0.0f;

    std::vector<float> input(dim);
    BuildInputForDim(features, action->getName(), input.data(), dim);
    return model->Forward(input.data(), dim);
}

float MlScorer::ScoreDuel(PlayerbotAI* botAI, Action* action, CombatFeatureVector const& features)
{
    if (!attemptedLoad)
        Reload();

    if (!action || !botAI || !botAI->GetBot())
        return 0.0f;

    return ScoreWithModel(ModelFor(botAI->GetBot()->getClass()), action, features);
}

bool MlScorer::HasTeacherFor(uint8 playerClass)
{
    if (!attemptedLoad)
        Reload();
    return TeacherFor(playerClass) != nullptr;
}

float MlScorer::ScoreDuelTeacher(PlayerbotAI* botAI, Action* action, CombatFeatureVector const& features)
{
    if (!attemptedLoad)
        Reload();

    if (!action || !botAI || !botAI->GetBot())
        return 0.0f;

    return ScoreWithModel(TeacherFor(botAI->GetBot()->getClass()), action, features);
}

bool MlScorer::HasMultiLogitFor(uint8 playerClass)
{
    if (!attemptedLoad)
        Reload();
    MlMlpModel const* model = ModelFor(playerClass);
    return model && model->IsMultiLogit();
}

MlMlpModel const* MlScorer::MovementModelFor(uint8 playerClass) const
{
    auto it = movementModels.find(playerClass);
    if (it != movementModels.end() && it->second.IsLoaded())
        return &it->second;
    return nullptr;
}

bool MlScorer::HasMovementModelFor(uint8 playerClass)
{
    if (!attemptedLoad)
        Reload();
    return MovementModelFor(playerClass) != nullptr;
}

bool MlScorer::ScoreMovement(uint8 playerClass, CombatFeatureVector const& features, float* outLogits, size_t outLen)
{
    if (!attemptedLoad)
        Reload();

    MlMlpModel const* model = MovementModelFor(playerClass);
    // Movement heads are strictly 90-D multi-logit (DEC-039); anything else falls back to scripted.
    if (!model || !model->IsMultiLogit() || model->InputDim() != ML_MOVE_INPUT_DIM || model->OutputDim() != outLen)
        return false;

    return model->ForwardMulti(features.data(), features.size(), outLogits, outLen);
}

bool MlScorer::ScoreSpellbook(PlayerbotAI* botAI, CombatFeatureVector const& features,
                              std::vector<MlDuelSpellCandidate> const& legal, std::vector<float>& outLogits)
{
    if (!attemptedLoad)
        Reload();

    outLogits.clear();
    if (!botAI || !botAI->GetBot() || legal.empty())
        return false;

    MlMlpModel const* model = ModelFor(botAI->GetBot()->getClass());
    if (!model || !model->IsMultiLogit())
        return false;

    size_t const dim = model->InputDim();
    if (dim != CF_ABILITY_FEATURE_COUNT && dim != ML_INPUT_DIM_NO_ACTION_ID && dim != ML_INPUT_DIM)
        return false;

    std::vector<float> input(dim, 0.0f);
    size_t nFeat = (std::min)(static_cast<size_t>(CF_ABILITY_FEATURE_COUNT), dim);
    for (size_t i = 0; i < nFeat; ++i)
        input[i] = features[i];

    std::vector<float> full(model->OutputDim(), 0.0f);
    if (!model->ForwardMulti(input.data(), dim, full.data(), full.size()))
        return false;

    outLogits.reserve(legal.size());
    for (MlDuelSpellCandidate const& cand : legal)
    {
        int idx = model->VocabIndex(cand.spellId);
        if (idx < 0)
            outLogits.push_back(-std::numeric_limits<float>::infinity());
        else
            outLogits.push_back(full[static_cast<size_t>(idx)]);
    }
    return true;
}
