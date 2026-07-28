/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef PLAYERBOTS_MLSCORER_H
#define PLAYERBOTS_MLSCORER_H

#include <string>
#include <unordered_map>
#include <vector>

#include "CombatDecisionFeatures.h"
#include "Define.h"
#include "MlDuelSpellPool.h"
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

    // DEC-026: S1 teacher scalar score (separate teacher PBML paths).
    float ScoreDuelTeacher(PlayerbotAI* botAI, Action* action, CombatFeatureVector const& features);
    bool HasTeacherFor(uint8 playerClass);

    // S2 multi-logit: one forward over frozen spell-id vocab; illegal logits masked to -inf.
    // Returns false if model is not multi-logit for this class.
    bool ScoreSpellbook(PlayerbotAI* botAI, CombatFeatureVector const& features,
                        std::vector<MlDuelSpellCandidate> const& legal, std::vector<float>& outLogits);

    void BuildInput(CombatFeatureVector const& features, std::string const& actionName, float* out) const;
    void BuildInputForDim(CombatFeatureVector const& features, std::string const& actionName, float* out,
                          size_t outDim) const;

    // Expose model shape for Engine policy routing.
    bool HasMultiLogitFor(uint8 playerClass);

private:
    MlScorer() = default;
    MlMlpModel const* ModelFor(uint8 playerClass) const;
    MlMlpModel const* TeacherFor(uint8 playerClass) const;
    float ScoreWithModel(MlMlpModel const* model, Action* action, CombatFeatureVector const& features) const;

    MlMlpModel fallbackModel;
    std::unordered_map<uint8, MlMlpModel> classModels;
    std::unordered_map<uint8, MlMlpModel> teacherModels;
    bool attemptedLoad = false;
};

#define sMlScorer MlScorer::instance()

#endif
