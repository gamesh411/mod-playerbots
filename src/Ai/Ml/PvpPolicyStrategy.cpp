/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "PvpPolicyStrategy.h"

#include "CombatDecisionFeatures.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

float PvpPolicyMultiplier::GetValue(Action* action)
{
    if (!sPlayerbotAIConfig.pvpPolicyEnabled || !action)
        return 1.0f;

    Player* bot = botAI->GetBot();
    if (!bot || !(bot->InBattleground() || bot->InArena()))
        return 1.0f;

    CombatFeatureVector const features = AI_VALUE(CombatFeatureVector, "combat decision features");
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

    // Strong interrupt bias in rated/arena-like pressure.
    if (CombatDecisionUtil::IsInterruptAction(name) && (targetCasting || hasEnemyHealer))
        score *= inArena ? 1.55f : 1.40f;

    // Healer lockdown is the highest-leverage arena call.
    if (CombatDecisionUtil::IsEnemyHealerAction(name) && hasEnemyHealer)
        score *= inArena ? 1.65f : 1.50f;

    if (CombatDecisionUtil::IsCrowdControlAction(name) && (hasEnemyHealer || enemyPlayerNear))
        score *= inArena ? 1.35f : 1.20f;

    if (CombatDecisionUtil::IsDefensiveAction(name) && (selfCritical || (selfLow && enemyPlayerNear)))
        score *= 1.50f;

    // Peel / protect partner in arena.
    if (inArena && partnerCritical &&
        (CombatDecisionUtil::IsCrowdControlAction(name) || CombatDecisionUtil::IsDefensiveAction(name) ||
         name.find("hand of freedom") != std::string::npos || name.find("blessing of freedom") != std::string::npos ||
         name.find("cleanse") != std::string::npos || name.find("dispel") != std::string::npos))
        score *= 1.30f;

    // Prefer player targets over NPCs in BG when both exist.
    if (name == "attack enemy player" || name == "attack enemy flag carrier")
        score *= 1.45f;

    if (enemyPlayerNear && !targetIsPlayer && name.find("attack") != std::string::npos &&
        name.find("enemy player") == std::string::npos && name.find("flag") == std::string::npos)
        score *= 0.70f;

    // Flag carrier urgency (WSG/EY).
    if (name.find("flag") != std::string::npos)
        score *= 1.25f;

    if (score < 0.30f)
        score = 0.30f;
    if (score > 1.90f)
        score = 1.90f;

    return score;
}

void PvpPolicyStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new PvpPolicyMultiplier(botAI));
}

void PvpPolicyStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Keep pressure on enemy players; complements AttackEnemyPlayersStrategy with higher combat urgency.
    triggers.push_back(new TriggerNode("enemy player near", { NextAction("attack enemy player", ACTION_HIGH + 8.0f) }));
    triggers.push_back(
        new TriggerNode("enemy flagcarrier near", { NextAction("attack enemy flag carrier", ACTION_RAID + 2.0f) }));
}
