/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "HybridRelevanceStrategy.h"

#include "CombatDecisionFeatures.h"
#include "GenericSpellActions.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

float HybridRelevanceMultiplier::GetValue(Action* action)
{
    if (!sPlayerbotAIConfig.hybridRelevanceEnabled || !action)
        return 1.0f;

    CombatFeatureVector const features = AI_VALUE(CombatFeatureVector, "combat decision features");
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

    // Interrupts: valuable in PvP and PvE when the target (or a healer) is casting.
    if (CombatDecisionUtil::IsInterruptAction(name) && (targetCasting || hasEnemyHealer))
        score *= 1.35f;

    // Focus enemy healers — helps arena/BG and also dungeon healer adds casting heals.
    if (CombatDecisionUtil::IsEnemyHealerAction(name) && hasEnemyHealer)
        score *= 1.45f;

    // Defensives when dying.
    if (CombatDecisionUtil::IsDefensiveAction(name) && (selfCritical || (selfLow && enemyPlayerNear)))
        score *= 1.40f;

    // Healing priority when party members are low (PvE + PvP).
    if ((dynamic_cast<CastHealingSpellAction*>(action) || CombatDecisionUtil::IsHealActionName(name)) &&
        (partyNeedsHeal || selfLow))
        score *= 1.25f;

    // Prefer instants under pressure from enemy players (also helps against caster packs).
    if (enemyPlayerNear && CombatDecisionUtil::IsInstantPreferredAction(name))
        score *= 1.12f;

    // Soft-penalize long filler casts when an enemy player is on us and we are low.
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

    // Clamp so we never invent emergency-level relevance or zero out legal actions accidentally.
    if (score < 0.35f)
        score = 0.35f;
    if (score > 1.75f)
        score = 1.75f;

    return score;
}

void HybridRelevanceStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new HybridRelevanceMultiplier(botAI));
}
