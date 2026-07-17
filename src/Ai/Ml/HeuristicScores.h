/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef PLAYERBOTS_HEURISTICSCORES_H
#define PLAYERBOTS_HEURISTICSCORES_H

#include "CombatDecisionFeatures.h"

namespace HeuristicScores
{
// Action-type flags used as extra MLP inputs (must match trainer / AF_COUNT).
void FillActionFlags(std::string const& name, float outFlags[8]);
// FNV-1a name fingerprint (4 floats in [-1,1]); must match tools/ml/action_flags.py.
void FillActionIdFeatures(std::string const& name, float outId[4]);
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

// Disambiguates same-flag actions (frostbolt vs fireball). Not logged — recomputed from action name.
static constexpr size_t AF_ID_COUNT = 4;
static constexpr size_t ML_INPUT_DIM = CF_FEATURE_COUNT + AF_COUNT + AF_ID_COUNT; // 82
// Pre-action-id duel PBML (70+8). Still loadable; cannot separate same-flag mage bolts.
static constexpr size_t ML_INPUT_DIM_NO_ACTION_ID = CF_FEATURE_COUNT + AF_COUNT; // 78
// Legacy PBML1 (pre-duel packs): 12 core features + 8 action flags.
static constexpr size_t ML_INPUT_DIM_V1 = 12 + AF_COUNT;

#endif
