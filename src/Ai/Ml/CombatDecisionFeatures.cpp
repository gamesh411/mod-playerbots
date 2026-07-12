/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "CombatDecisionFeatures.h"

#include <algorithm>
#include <sstream>

#include "Playerbots.h"
#include "ServerFacade.h"

CombatFeatureVector CombatDecisionFeaturesValue::Calculate()
{
    CombatFeatureVector features{};
    features.fill(0.0f);

    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsInWorld())
        return features;

    features[CF_SELF_HEALTH] = AI_VALUE2(uint8, "health", "self target") / 100.0f;
    features[CF_SELF_MANA] = AI_VALUE2(bool, "has mana", "self target")
                                 ? AI_VALUE2(uint8, "mana", "self target") / 100.0f
                                 : 1.0f;

    Unit* target = AI_VALUE(Unit*, "current target");
    if (target && target->IsAlive())
    {
        features[CF_TARGET_HEALTH] = target->GetHealthPct() / 100.0f;
        features[CF_TARGET_IS_PLAYER] = target->IsPlayer() ? 1.0f : 0.0f;
        features[CF_TARGET_IS_CASTING] = target->IsNonMeleeSpellCast(false) ? 1.0f : 0.0f;
    }

    // Detect an enemy casting a positive (typically heal) spell without requiring a specific interrupt name.
    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const& guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsAlive())
            continue;

        Spell* genericSpell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (genericSpell && genericSpell->m_spellInfo && genericSpell->m_spellInfo->IsPositive())
        {
            features[CF_HAS_ENEMY_HEALER] = 1.0f;
            break;
        }

        Spell* channelSpell = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        if (channelSpell && channelSpell->m_spellInfo && channelSpell->m_spellInfo->IsPositive())
        {
            features[CF_HAS_ENEMY_HEALER] = 1.0f;
            break;
        }
    }

    Unit* enemyPlayer = AI_VALUE(Unit*, "enemy player target");
    features[CF_ENEMY_PLAYER_NEAR] = (enemyPlayer && enemyPlayer->IsAlive() &&
                                      ServerFacade::instance().IsDistanceLessOrEqualThan(
                                          ServerFacade::instance().GetDistance2d(bot, enemyPlayer),
                                          sPlayerbotAIConfig.sightDistance))
                                         ? 1.0f
                                         : 0.0f;

    uint8 partyLow = 0;
    Group* group = bot->GetGroup();
    if (group)
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
                continue;
            if (member->GetHealthPct() < sPlayerbotAIConfig.lowHealth)
                ++partyLow;
        }
    }
    features[CF_PARTY_LOW_HEALTH] = std::min(1.0f, partyLow / 3.0f);

    features[CF_IN_BATTLEGROUND] = bot->InBattleground() ? 1.0f : 0.0f;
    features[CF_IN_ARENA] = bot->InArena() ? 1.0f : 0.0f;
    features[CF_ATTACKER_COUNT] = std::min(1.0f, AI_VALUE(uint8, "attacker count") / 5.0f);
    features[CF_SELF_HAS_CONTROL_LOSS] = bot->HasUnitState(UNIT_STATE_LOST_CONTROL) ? 1.0f : 0.0f;

    return features;
}

std::string const CombatDecisionFeaturesValue::Format()
{
    CombatFeatureVector const& f = value;
    std::ostringstream out;
    out << "hp=" << f[CF_SELF_HEALTH] << " tgtHp=" << f[CF_TARGET_HEALTH]
        << " cast=" << f[CF_TARGET_IS_CASTING] << " ePlayer=" << f[CF_ENEMY_PLAYER_NEAR]
        << " eHeal=" << f[CF_HAS_ENEMY_HEALER] << " bg=" << f[CF_IN_BATTLEGROUND]
        << " arena=" << f[CF_IN_ARENA];
    return out.str();
}

namespace CombatDecisionUtil
{
bool IsInterruptAction(std::string const& name)
{
    return name.find("kick") != std::string::npos || name.find("pummel") != std::string::npos ||
           name.find("counterspell") != std::string::npos || name.find("mind freeze") != std::string::npos ||
           name.find("wind shear") != std::string::npos || name.find("spell lock") != std::string::npos ||
           name.find("shield bash") != std::string::npos || name.find("strangulate") != std::string::npos ||
           name.find("silencing shot") != std::string::npos || name.find("arcane torrent") != std::string::npos ||
           name.find("deadly throw") != std::string::npos || name.find("gouge") != std::string::npos;
}

bool IsEnemyHealerAction(std::string const& name) { return name.find("on enemy healer") != std::string::npos; }

bool IsDefensiveAction(std::string const& name)
{
    return name.find("ice block") != std::string::npos || name.find("divine shield") != std::string::npos ||
           name.find("divine protection") != std::string::npos || name.find("barkskin") != std::string::npos ||
           name.find("survival instincts") != std::string::npos || name.find("shield wall") != std::string::npos ||
           name.find("last stand") != std::string::npos || name.find("cloak of shadows") != std::string::npos ||
           name.find("dispersion") != std::string::npos || name.find("pain suppression") != std::string::npos ||
           name.find("hand of protection") != std::string::npos || name.find("blessing of protection") != std::string::npos ||
           name.find("deterrence") != std::string::npos || name.find("die by the sword") != std::string::npos ||
           name.find("anti-magic shell") != std::string::npos || name.find("icebound fortitude") != std::string::npos;
}

bool IsCrowdControlAction(std::string const& name)
{
    return name.find("polymorph") != std::string::npos || name.find("fear") != std::string::npos ||
           name.find("hammer of justice") != std::string::npos || name.find("repentance") != std::string::npos ||
           name.find("blind") != std::string::npos || name.find("hex") != std::string::npos ||
           name.find("cyclone") != std::string::npos || name.find("sap") != std::string::npos ||
           name.find("freezing trap") != std::string::npos || name.find("wyvern sting") != std::string::npos ||
           name.find("scatter shot") != std::string::npos || name.find("death coil") != std::string::npos ||
           name.find("banish") != std::string::npos || name.find("seduction") != std::string::npos;
}

bool IsHealActionName(std::string const& name)
{
    return name.find("heal") != std::string::npos || name.find("flash") != std::string::npos ||
           name.find("renew") != std::string::npos || name.find("rejuvenation") != std::string::npos ||
           name.find("regrowth") != std::string::npos || name.find("nourish") != std::string::npos ||
           name.find("lesser heal") != std::string::npos || name.find("holy light") != std::string::npos ||
           name.find("flash of light") != std::string::npos || name.find("lay on hands") != std::string::npos ||
           name.find("righteous defense") != std::string::npos || name.find("chain heal") != std::string::npos ||
           name.find("riptide") != std::string::npos || name.find("healing wave") != std::string::npos ||
           name.find("lesser healing wave") != std::string::npos;
}

bool IsInstantPreferredAction(std::string const& name)
{
    return IsInterruptAction(name) || IsDefensiveAction(name) || IsCrowdControlAction(name) ||
           name.find("trinket") != std::string::npos || name.find("vanish") != std::string::npos ||
           name.find("shadowstep") != std::string::npos || name.find("blink") != std::string::npos ||
           name.find("disengage") != std::string::npos;
}
}  // namespace CombatDecisionUtil
