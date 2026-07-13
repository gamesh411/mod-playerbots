/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "HeuristicScores.h"

#include "GenericSpellActions.h"
#include "PlayerbotAIConfig.h"
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

float Hybrid(PlayerbotAI* botAI, Action* action, CombatFeatureVector const& features)
{
    if (!action)
        return 1.0f;

    AiObjectContext* context = botAI->GetAiObjectContext();
    Player* bot = botAI->GetBot();
    if (!context || !bot)
        return 1.0f;

    std::string const name = action->getName();
    float score = 1.0f;

    bool const targetCasting = features[CF_TARGET_IS_CASTING] > 0.5f;
    bool const enemyPlayerNear = features[CF_ENEMY_PLAYER_NEAR] > 0.5f;
    bool const hasEnemyHealer = features[CF_HAS_ENEMY_HEALER] > 0.5f;
    bool const partyNeedsHeal = features[CF_PARTY_LOW_HEALTH] > 0.15f;
    bool const selfCritical = features[CF_SELF_HEALTH] > 0.0f &&
                              features[CF_SELF_HEALTH] * 100.0f < sPlayerbotAIConfig.criticalHealth;
    bool const selfLow = features[CF_SELF_HEALTH] > 0.0f &&
                         features[CF_SELF_HEALTH] * 100.0f < sPlayerbotAIConfig.lowHealth;

    if (CombatDecisionUtil::IsInterruptAction(name) && (targetCasting || hasEnemyHealer))
        score *= 1.35f;

    if (CombatDecisionUtil::IsEnemyHealerAction(name) && hasEnemyHealer)
        score *= 1.45f;

    if (CombatDecisionUtil::IsDefensiveAction(name) && (selfCritical || (selfLow && enemyPlayerNear)))
        score *= 1.40f;

    if ((dynamic_cast<CastHealingSpellAction*>(action) || CombatDecisionUtil::IsHealActionName(name)) &&
        (partyNeedsHeal || selfLow))
        score *= 1.25f;

    if (enemyPlayerNear && CombatDecisionUtil::IsInstantPreferredAction(name))
        score *= 1.12f;

    if (enemyPlayerNear && selfLow && dynamic_cast<CastSpellAction*>(action) &&
        !CombatDecisionUtil::IsInstantPreferredAction(name) && !CombatDecisionUtil::IsHealActionName(name))
    {
        CastSpellAction* spellAction = dynamic_cast<CastSpellAction*>(action);
        uint32 spellId = AI_VALUE2(uint32, "spell id", spellAction->getSpell());
        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId))
        {
            if (info->CalcCastTime(bot) >= 1500)
                score *= 0.75f;
        }
    }

    if (score < 0.35f)
        score = 0.35f;
    if (score > 1.75f)
        score = 1.75f;
    return score;
}

float PvpPolicy(PlayerbotAI* /*botAI*/, Action* action, CombatFeatureVector const& features)
{
    if (!action)
        return 1.0f;

    std::string const name = action->getName();
    float score = 1.0f;

    bool const inArena = features[CF_IN_ARENA] > 0.5f;
    bool const enemyPlayerNear = features[CF_ENEMY_PLAYER_NEAR] > 0.5f;
    bool const hasEnemyHealer = features[CF_HAS_ENEMY_HEALER] > 0.5f;
    bool const targetCasting = features[CF_TARGET_IS_CASTING] > 0.5f;
    bool const targetIsPlayer = features[CF_TARGET_IS_PLAYER] > 0.5f;
    bool const selfCritical = features[CF_SELF_HEALTH] > 0.0f &&
                              features[CF_SELF_HEALTH] * 100.0f < sPlayerbotAIConfig.criticalHealth;
    bool const selfLow = features[CF_SELF_HEALTH] > 0.0f &&
                         features[CF_SELF_HEALTH] * 100.0f < sPlayerbotAIConfig.lowHealth;
    bool const partnerCritical = features[CF_PARTY_LOW_HEALTH] > 0.3f;

    if (CombatDecisionUtil::IsInterruptAction(name) && (targetCasting || hasEnemyHealer))
        score *= inArena ? 1.55f : 1.40f;

    if (CombatDecisionUtil::IsEnemyHealerAction(name) && hasEnemyHealer)
        score *= inArena ? 1.65f : 1.50f;

    if (CombatDecisionUtil::IsCrowdControlAction(name) && (hasEnemyHealer || enemyPlayerNear))
        score *= inArena ? 1.35f : 1.20f;

    if (CombatDecisionUtil::IsDefensiveAction(name) && (selfCritical || (selfLow && enemyPlayerNear)))
        score *= 1.50f;

    if (inArena && partnerCritical &&
        (CombatDecisionUtil::IsCrowdControlAction(name) || CombatDecisionUtil::IsDefensiveAction(name) ||
         name.find("hand of freedom") != std::string::npos || name.find("blessing of freedom") != std::string::npos ||
         name.find("cleanse") != std::string::npos || name.find("dispel") != std::string::npos))
        score *= 1.30f;

    if (name == "attack enemy player" || name == "attack enemy flag carrier")
        score *= 1.45f;

    if (enemyPlayerNear && !targetIsPlayer && name.find("attack") != std::string::npos &&
        name.find("enemy player") == std::string::npos && name.find("flag") == std::string::npos)
        score *= 0.70f;

    if (name.find("flag") != std::string::npos)
        score *= 1.25f;

    if (score < 0.30f)
        score = 0.30f;
    if (score > 1.90f)
        score = 1.90f;
    return score;
}
}  // namespace HeuristicScores
