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

// Curriculum duel ranker (S1/S2). Single model path — no hybrid/pvp blend (DEC-021).
class MlScorer
{
public:
    static MlScorer& instance();

    void Reload();
    bool ModelLoaded() const { return duelModel.IsLoaded(); }

    // Raw network score for a candidate action (higher = preferred). Returns 0 if unloaded.
    float ScoreDuel(PlayerbotAI* botAI, Action* action, CombatFeatureVector const& features);

    void BuildInput(CombatFeatureVector const& features, std::string const& actionName, float* out) const;
    void BuildInputForDim(CombatFeatureVector const& features, std::string const& actionName, float* out,
                          size_t outDim) const;

private:
    MlScorer() = default;
    MlMlpModel duelModel;
    bool attemptedLoad = false;
};

#define sMlScorer MlScorer::instance()

#endif
