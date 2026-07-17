/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef PLAYERBOTS_MLSCORER_H
#define PLAYERBOTS_MLSCORER_H

#include <string>
#include <unordered_map>

#include "CombatDecisionFeatures.h"
#include "Define.h"
#include "MlMlpModel.h"

class Action;
class PlayerbotAI;

// Curriculum duel ranker (S1/S2). Per-class PBML with optional single-path fallback (DEC-021 / DEC-025).
class MlScorer
{
public:
    static MlScorer& instance();

    void Reload();
    // True if any duel model is loaded (legacy callers).
    bool ModelLoaded() const;
    // DEC-025: prefer per-class PBML; fall back to MlModelPathDuel.
    bool HasModelFor(uint8 playerClass) const;

    // Raw network score for a candidate action (higher = preferred). Returns 0 if unloaded.
    float ScoreDuel(PlayerbotAI* botAI, Action* action, CombatFeatureVector const& features);

    void BuildInput(CombatFeatureVector const& features, std::string const& actionName, float* out) const;
    void BuildInputForDim(CombatFeatureVector const& features, std::string const& actionName, float* out,
                          size_t outDim) const;

private:
    MlScorer() = default;
    MlMlpModel const* ModelFor(uint8 playerClass) const;

    MlMlpModel fallbackModel;
    std::unordered_map<uint8, MlMlpModel> classModels;
    bool attemptedLoad = false;
};

#define sMlScorer MlScorer::instance()

#endif
