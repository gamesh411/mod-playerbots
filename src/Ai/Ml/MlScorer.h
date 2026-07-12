/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef PLAYERBOTS_MLSCORER_H
#define PLAYERBOTS_MLSCORER_H

#include <string>

#include "CombatDecisionFeatures.h"
#include "MlMlpModel.h"

class Action;
class PlayerbotAI;

class MlScorer
{
public:
    static MlScorer& instance();

    void Reload();
    bool HybridModelLoaded() const { return hybridModel.IsLoaded(); }
    bool PvpModelLoaded() const { return pvpModel.IsLoaded(); }

    // Blends heuristic with MLP when loaded and alpha > 0.
    float ScoreHybrid(PlayerbotAI* botAI, Action* action, CombatFeatureVector const& features);
    float ScorePvp(PlayerbotAI* botAI, Action* action, CombatFeatureVector const& features);

    void BuildInput(CombatFeatureVector const& features, std::string const& actionName, float* out18) const;
    float RawToMultiplier(float raw, float lo, float hi) const;

private:
    MlScorer() = default;
    MlMlpModel hybridModel;
    MlMlpModel pvpModel;
    bool attemptedLoad = false;
};

#define sMlScorer MlScorer::instance()

#endif
