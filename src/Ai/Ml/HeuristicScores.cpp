/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "HeuristicScores.h"

#include "Playerbots.h"

namespace HeuristicScores
{
void FillActionFlags(std::string const& name, float outFlags[8])
{
    for (size_t i = 0; i < AF_COUNT; ++i)
        outFlags[i] = 0.0f;

    outFlags[AF_INTERRUPT] = CombatDecisionUtil::IsInterruptAction(name) ? 1.0f : 0.0f;
    // Healer-pressure role: explicit healer focus, or interrupt (paired with CF_HAS_ENEMY_HEALER in features).
    outFlags[AF_ENEMY_HEALER] = CombatDecisionUtil::IsEnemyHealerAction(name) ? 1.0f : 0.0f;
    outFlags[AF_DEFENSIVE] = CombatDecisionUtil::IsDefensiveAction(name) ? 1.0f : 0.0f;
    outFlags[AF_CC] = CombatDecisionUtil::IsCrowdControlAction(name) ? 1.0f : 0.0f;
    outFlags[AF_HEAL] = CombatDecisionUtil::IsHealActionName(name) ? 1.0f : 0.0f;
    outFlags[AF_INSTANT] = CombatDecisionUtil::IsInstantPreferredAction(name) ? 1.0f : 0.0f;
    outFlags[AF_DAMAGE] = CombatDecisionUtil::IsDamageAction(name) ? 1.0f : 0.0f;
    outFlags[AF_FOCUS_PLAYER] = CombatDecisionUtil::IsFocusPlayerAction(name) ? 1.0f : 0.0f;
}

void FillActionIdFeatures(std::string const& name, float outId[4])
{
    // FNV-1a 32-bit over lowercase UTF-8 bytes; 4 bytes → [-1, 1]. Matches tools/ml/action_flags.py.
    uint32 h = 2166136261u;
    for (unsigned char c : name)
    {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<unsigned char>(c + 32);
        h ^= c;
        h *= 16777619u;
    }
    for (size_t i = 0; i < AF_ID_COUNT; ++i)
        outId[i] = float((h >> (8u * i)) & 0xffu) / 127.5f - 1.0f;
}
}  // namespace HeuristicScores
