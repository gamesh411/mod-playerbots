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

    // Width of the pre-movement slice the frozen S-track ability heads were trained on
    // (DEC-025/026, 82-D PBML). Kept as a marker so those artifacts still load; duel_v6 ability
    // heads consume the whole vector instead (DEC-042/043).
    CF_LEGACY_ABILITY_FEATURE_COUNT,

    // --- Pack CF_MOVE (70-89): kinematics, impairment, walkability probes (DEC-036) ---
    // Angles foe-bearing-relative, normalized to [-1, 1] (angle / pi); speeds / base run.
    CF_MOVE_SELF_SPEED_FRAC = CF_LEGACY_ABILITY_FEATURE_COUNT,
    CF_MOVE_SELF_HEADING_REL,
    CF_MOVE_FOE_SPEED_FRAC,
    CF_MOVE_FOE_HEADING_REL,
    CF_MOVE_SELF_FACING_OFFSET,
    CF_MOVE_FOE_FACING_OFFSET,
    CF_MOVE_CLOSING_SPEED,
    CF_MOVE_SELF_AIRBORNE,
    CF_MOVE_SELF_SNARE_FRAC,
    CF_MOVE_SELF_ROOTED,
    CF_MOVE_FOE_SNARE_FRAC,
    CF_MOVE_FOE_ROOTED,
    // 8 foe-relative walkability probes (height delta + LoS at ProbeRangeYd), shared with the
    // executor safety clamp. N = toward foe, 45-degree steps counterclockwise.
    CF_MOVE_PROBE_N,
    CF_MOVE_PROBE_NE,
    CF_MOVE_PROBE_E,
    CF_MOVE_PROBE_SE,
    CF_MOVE_PROBE_S,
    CF_MOVE_PROBE_SW,
    CF_MOVE_PROBE_W,
    CF_MOVE_PROBE_NW,

    // Input width of the frozen M1 movement heads (DEC-039). Packs only ever append, so this
    // prefix slice stays stable forever and the frozen PBML artifacts remain byte-identical
    // however far the vector grows (DEC-043).
    CF_MOVE_HEAD_FEATURE_COUNT,

    // --- Pack CF_PET (90-99): role-based minion state, self/foe symmetric (DEC-043) ---
    // Readiness bits follow the DuelCD convention (1 = known and off CD, 0 = on CD or absent) and
    // cover non-autocast command spells only - autocast is pet AI, not a decision (DEC-032).
    CF_SELF_PET_COUNT = CF_MOVE_HEAD_FEATURE_COUNT,
    CF_SELF_PET_HEALTH,
    CF_SELF_PET_KICK_READY,
    CF_SELF_PET_CC_READY,
    CF_SELF_PET_UTILITY_READY,
    CF_FOE_PET_COUNT,
    CF_FOE_PET_HEALTH,
    CF_FOE_PET_KICK_READY,
    CF_FOE_PET_CC_READY,
    CF_FOE_PET_UTILITY_READY,

    // --- Pack CF_FORM (100-111): class-relative form one-hot (DEC-043) ---
    // All-zero = base / no form, the spec-tab idiom. Slot meaning is class-relative; see
    // FormSlot() in CombatDecisionFeatures.cpp for the per-class mapping.
    CF_SELF_FORM_0,
    CF_SELF_FORM_1,
    CF_SELF_FORM_2,
    CF_SELF_FORM_3,
    CF_SELF_FORM_4,
    CF_SELF_FORM_5,
    CF_FOE_FORM_0,
    CF_FOE_FORM_1,
    CF_FOE_FORM_2,
    CF_FOE_FORM_3,
    CF_FOE_FORM_4,
    CF_FOE_FORM_5,

    CF_FEATURE_COUNT
};

// Slots per side in the CF_FORM pack; a class with more forms than this needs a pack extension,
// not a renumber.
static constexpr size_t CF_FORM_SLOTS = 6;

// Frozen artifacts are pinned to these widths, and CSV revisions are named after them, so a
// renumber has to fail here rather than silently mis-slice every PBML and log row (DEC-043).
static_assert(CF_LEGACY_ABILITY_FEATURE_COUNT == 70, "S-track ability heads (78/82-D PBML) slice f0..f69");
static_assert(CF_MOVE_HEAD_FEATURE_COUNT == 90, "Frozen M1 movement heads are 90-D over f0..f89");
static_assert(CF_FEATURE_COUNT == 112, "duel_v6 logs 112 state columns (DEC-043)");

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
