/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef PLAYERBOTS_HEURISTICSCORES_H
#define PLAYERBOTS_HEURISTICSCORES_H

#include "CombatDecisionFeatures.h"

class Action;
class PlayerbotAI;

namespace HeuristicScores
{
float Hybrid(PlayerbotAI* botAI, Action* action, CombatFeatureVector const& features);
float PvpPolicy(PlayerbotAI* botAI, Action* action, CombatFeatureVector const& features);

// Action-type flags used as extra MLP inputs (must match trainer / AF_COUNT).
void FillActionFlags(std::string const& name, float outFlags[8]);
}  // namespace HeuristicScores

enum ActionFlagIndex : size_t
{
    AF_INTERRUPT = 0,      // any interrupt ability (spell-agnostic role)
    AF_ENEMY_HEALER,       // healer pressure / interrupt-into-heal intent
    AF_DEFENSIVE,
    AF_CC,
    AF_HEAL,
    AF_INSTANT,
    AF_DAMAGE,             // damaging pressure
    AF_FOCUS_PLAYER,       // focus enemy player / flag carrier
    AF_COUNT = 8
};

static constexpr size_t ML_INPUT_DIM = CF_FEATURE_COUNT + AF_COUNT;
// Legacy PBML1 (pre-duel packs): 12 core features + 8 action flags.
static constexpr size_t ML_INPUT_DIM_V1 = 12 + AF_COUNT;

#endif
