/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef PLAYERBOTS_COMBATDECISIONFEATURES_H
#define PLAYERBOTS_COMBATDECISIONFEATURES_H

#include <array>
#include <string>

#include "Value.h"

class PlayerbotAI;

// Compact feature vector shared by hybrid relevance, PvP policy, and duel logging.
// Inventory / orthogonality: docs/ml/FEATURES.md
// Append-only within packs; bump duel CSV schema (v2, v3, …) when this layout grows.
enum CombatFeatureIndex : size_t
{
    // --- Pack Core (legacy; keep indices 0-11 stable for 20-D PBML1) ---
    CF_SELF_HEALTH = 0,
    CF_SELF_MANA,
    CF_TARGET_HEALTH,
    CF_TARGET_IS_PLAYER,
    CF_TARGET_IS_CASTING,
    CF_HAS_ENEMY_HEALER,
    CF_ENEMY_PLAYER_NEAR,
    CF_PARTY_LOW_HEALTH,
    CF_IN_BATTLEGROUND,
    CF_IN_ARENA,
    CF_ATTACKER_COUNT,
    CF_SELF_HAS_CONTROL_LOSS,

    // --- Pack Class: self + foe identity (10 class slots + 3 talent tabs each) ---
    // Class slot order: Warrior, Paladin, Hunter, Rogue, Priest, DK, Shaman, Mage, Warlock, Druid
    CF_SELF_CLASS_0,
    CF_SELF_CLASS_1,
    CF_SELF_CLASS_2,
    CF_SELF_CLASS_3,
    CF_SELF_CLASS_4,
    CF_SELF_CLASS_5,
    CF_SELF_CLASS_6,
    CF_SELF_CLASS_7,
    CF_SELF_CLASS_8,
    CF_SELF_CLASS_9,
    CF_SELF_SPEC_0,
    CF_SELF_SPEC_1,
    CF_SELF_SPEC_2,
    CF_FOE_CLASS_0,
    CF_FOE_CLASS_1,
    CF_FOE_CLASS_2,
    CF_FOE_CLASS_3,
    CF_FOE_CLASS_4,
    CF_FOE_CLASS_5,
    CF_FOE_CLASS_6,
    CF_FOE_CLASS_7,
    CF_FOE_CLASS_8,
    CF_FOE_CLASS_9,
    CF_FOE_SPEC_0,
    CF_FOE_SPEC_1,
    CF_FOE_SPEC_2,

    // --- Pack DuelCD: readiness (1 = off CD / usable, 0 = on CD or unknown) ---
    CF_SELF_KICK_READY,
    CF_SELF_DEFENSIVE_READY,
    CF_SELF_OFFENSIVE_CD_READY,
    CF_SELF_GAPCLOSE_READY,
    CF_SELF_TRINKET_READY,
    CF_FOE_KICK_READY,
    CF_FOE_DEFENSIVE_READY,
    CF_FOE_OFFENSIVE_CD_READY,
    CF_FOE_GAPCLOSE_READY,
    CF_FOE_ROOT_READY,
    CF_FOE_TRINKET_READY,

    // --- Pack DuelDR: remaining CC effectiveness (1 full → 0 immune) ---
    CF_SELF_DR_STUN,
    CF_SELF_DR_SILENCE,
    CF_SELF_DR_ROOT,
    CF_SELF_DR_DISORIENT,
    CF_SELF_DR_FEAR,
    CF_FOE_DR_STUN,
    CF_FOE_DR_SILENCE,
    CF_FOE_DR_ROOT,
    CF_FOE_DR_DISORIENT,
    CF_FOE_DR_FEAR,
    CF_SELF_HAS_IMMUNITY,
    CF_FOE_HAS_IMMUNITY,

    // --- Pack DuelRange: positioning ---
    CF_DIST_NORM,
    CF_IN_MELEE,
    CF_IN_LOS,
    CF_SELF_FACING_FOE,
    CF_FOE_FACING_SELF,
    CF_BEHIND_FOE,

    // --- Pack FoeVitals / duel context ---
    CF_FOE_POWER,
    CF_FOE_HAS_CONTROL_LOSS,
    CF_IN_DUEL,

    CF_FEATURE_COUNT
};

using CombatFeatureVector = std::array<float, CF_FEATURE_COUNT>;

class CombatDecisionFeaturesValue : public CalculatedValue<CombatFeatureVector>
{
public:
    CombatDecisionFeaturesValue(PlayerbotAI* botAI)
        : CalculatedValue<CombatFeatureVector>(botAI, "combat decision features", 100)
    {
    }

    CombatFeatureVector Calculate() override;
    std::string const Format() override;
};

namespace CombatDecisionUtil
{
bool IsMetaAction(std::string const& name);
bool IsInterruptAction(std::string const& name);
bool IsEnemyHealerAction(std::string const& name);
bool IsDefensiveAction(std::string const& name);
bool IsCrowdControlAction(std::string const& name);
bool IsHealActionName(std::string const& name);
bool IsDamageAction(std::string const& name);
bool IsFocusPlayerAction(std::string const& name);
bool IsInstantPreferredAction(std::string const& name);
bool IsLoggableCombatAction(std::string const& name);
}  // namespace CombatDecisionUtil

#endif
