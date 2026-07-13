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

// Compact feature vector shared by hybrid relevance (Option B) and PvP policy (Option C).
// Layout is stable so an ONNX/export model can consume the same floats later.
enum CombatFeatureIndex : size_t
{
    CF_SELF_HEALTH = 0,
    CF_SELF_MANA,
    CF_TARGET_HEALTH,
    CF_TARGET_IS_PLAYER,
    CF_TARGET_IS_CASTING,
    CF_HAS_ENEMY_HEALER,  // enemy casting a positive/heal spell (spell-agnostic)
    CF_ENEMY_PLAYER_NEAR,
    CF_PARTY_LOW_HEALTH,
    CF_IN_BATTLEGROUND,
    CF_IN_ARENA,
    CF_ATTACKER_COUNT,
    CF_SELF_HAS_CONTROL_LOSS,
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
