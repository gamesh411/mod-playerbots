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
// DEC-042/043: duel_v6 ability heads consume the whole state vector, movement pack included.
static constexpr size_t ML_INPUT_DIM = CF_FEATURE_COUNT + AF_COUNT + AF_ID_COUNT; // 124
// Frozen S-track ability heads (DEC-036 era): 70-feature slice + flags + action id, and the
// same pair without the action-id pack. Still loadable so the duel-s1/duel-s2 replay profiles
// (DEC-040) keep scoring their certified artifacts.
static constexpr size_t ML_INPUT_DIM_LEGACY = CF_LEGACY_ABILITY_FEATURE_COUNT + AF_COUNT + AF_ID_COUNT; // 82
static constexpr size_t ML_INPUT_DIM_LEGACY_NO_ACTION_ID = CF_LEGACY_ABILITY_FEATURE_COUNT + AF_COUNT; // 78
// M1 movement heads are pinned to the 90-D prefix slice; later packs never reach them (DEC-043).
static constexpr size_t ML_MOVE_INPUT_DIM = CF_MOVE_HEAD_FEATURE_COUNT; // 90
// Legacy PBML1 (pre-duel packs): 12 core features + 8 action flags.
static constexpr size_t ML_INPUT_DIM_V1 = 12 + AF_COUNT;

#endif
