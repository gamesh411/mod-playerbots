/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "CombatDecisionFeatures.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "Playerbots.h"
#include "ServerFacade.h"

namespace
{
std::string ToLowerCopy(std::string const& s)
{
    std::string out = s;
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool ContainsAny(std::string const& hay, std::initializer_list<char const*> needles)
{
    for (char const* n : needles)
        if (hay.find(n) != std::string::npos)
            return true;
    return false;
}

bool UnitCastingPositive(Unit* unit)
{
    if (!unit)
        return false;
    Spell* genericSpell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (genericSpell && genericSpell->m_spellInfo && genericSpell->m_spellInfo->IsPositive())
        return true;
    Spell* channelSpell = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
    if (channelSpell && channelSpell->m_spellInfo && channelSpell->m_spellInfo->IsPositive())
        return true;
    return false;
}
}  // namespace

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
        // Spell-agnostic: any positive cast on the current target counts as heal pressure.
        if (UnitCastingPositive(target))
            features[CF_HAS_ENEMY_HEALER] = 1.0f;
    }

    // Scan attackers and nearby enemy players for positive (heal) casts — not tied to a spell name.
    auto markHealerIfPositive = [&](Unit* unit)
    {
        if (features[CF_HAS_ENEMY_HEALER] > 0.5f)
            return;
        if (unit && unit->IsAlive() && UnitCastingPositive(unit))
            features[CF_HAS_ENEMY_HEALER] = 1.0f;
    };

    GuidVector attackers = AI_VALUE(GuidVector, "attackers");
    for (ObjectGuid const& guid : attackers)
        markHealerIfPositive(botAI->GetUnit(guid));

    GuidVector nearbyPlayers = AI_VALUE(GuidVector, "nearest enemy players");
    for (ObjectGuid const& guid : nearbyPlayers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsAlive())
            continue;
        if (!ServerFacade::instance().IsDistanceLessOrEqualThan(ServerFacade::instance().GetDistance2d(bot, unit),
                                                                sPlayerbotAIConfig.sightDistance))
            continue;
        markHealerIfPositive(unit);
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
bool IsMetaAction(std::string const& name)
{
    std::string const n = ToLowerCopy(name);
    return ContainsAny(n,
                       {"set facing", "reach melee", "reach spell", "check mount", "check objective", "reset objective",
                        "move to objective", "move to start", "move to", "xp gain", "drop target", "dps assist",
                        "apply oil", "apply stone", "auto release", "self resurrect", "follow", "food", "drink",
                        "unstealth", "set behind", "set pet", "toggle pet", "cast greater blessing assignment",
                        "select new target", "update strategy", "chat", "emote", "rpg ", "travel", "grind", "loot",
                        "add all loot", "equip", "use stone", "use oil", "wait for", "guard", "stay", "follow master"});
}

bool IsInterruptAction(std::string const& name)
{
    std::string const n = ToLowerCopy(name);
    // Role flag: any interrupt — concrete spell name does not matter for the MLP.
    return ContainsAny(n, {"kick", "pummel", "counterspell", "mind freeze", "wind shear", "spell lock", "shield bash",
                           "strangulate", "silencing shot", "arcane torrent", "deadly throw", "gouge", "silence",
                           "bash"});
}

bool IsEnemyHealerAction(std::string const& name)
{
    std::string const n = ToLowerCopy(name);
    // Explicit healer-focus actions only. Heal-cast presence is CF_HAS_ENEMY_HEALER (spell-agnostic).
    return ContainsAny(n, {"on enemy healer", "enemy healer"});
}

bool IsDefensiveAction(std::string const& name)
{
    std::string const n = ToLowerCopy(name);
    return ContainsAny(n, {"ice block", "divine shield", "divine protection", "barkskin", "survival instincts",
                           "shield wall", "last stand", "cloak of shadows", "dispersion", "pain suppression",
                           "hand of protection", "blessing of protection", "deterrence", "die by the sword",
                           "anti-magic shell", "icebound fortitude", "shield block", "feign death", "vanish",
                           "fade", "hand of sacrifice", "blessing of sacrifice", "guardian spirit"});
}

bool IsCrowdControlAction(std::string const& name)
{
    std::string const n = ToLowerCopy(name);
    // Exclude "death coil" — it is primarily damage/fear hybrid and polluted CC flags.
    if (n.find("death coil") != std::string::npos)
        return false;
    return ContainsAny(n, {"polymorph", "fear", "hammer of justice", "repentance", "blind", "hex", "cyclone", "sap",
                           "freezing trap", "wyvern sting", "scatter shot", "banish", "seduction", "hibernate",
                           "shackle", "turn evil", "scare beast", "psychic scream", "howl of terror", "cheap shot",
                           "kidney shot", "deep freeze", "frost nova", "entangling roots", "nature's grasp"});
}

bool IsHealActionName(std::string const& name)
{
    std::string const n = ToLowerCopy(name);
    return ContainsAny(n, {"heal", "flash", "renew", "rejuvenation", "regrowth", "nourish", "holy light",
                           "flash of light", "lay on hands", "chain heal", "riptide", "healing wave",
                           "lesser healing wave", "penance", "circle of healing", "prayer of mending",
                           "prayer of healing", "binding heal", "wild growth", "lifebloom", "holy shock",
                           "gift of the naaru", "bandage"});
}

bool IsDamageAction(std::string const& name)
{
    if (IsHealActionName(name) || IsDefensiveAction(name) || IsCrowdControlAction(name) || IsMetaAction(name))
        return false;
    std::string const n = ToLowerCopy(name);
    return ContainsAny(n, {"attack", "strike", "shot", "bolt", "fireball", "frostbolt", "shadow bolt", "smite",
                           "wrath", "starfire", "lava", "chaos", "arcane blast", "arcane missiles", "mind blast",
                           "mind flay", "corruption", "immolate", "incinerate", "conflagrate", "haunt", "unstable affliction",
                           "serpent sting", "steady shot", "aimed shot", "multi-shot", "chimera", "explosive shot",
                           "mortal strike", "heroic strike", "slam", "execute", "bloodthirst", "whirlwind",
                           "sinister", "eviscerate", "envenom", "mutilate", "backstab", "hemorrhage",
                           "crusader", "judgement", "consecration", "exorcism", "hammer of wrath",
                           "lightning bolt", "earth shock", "flame shock", "lava burst", "stormstrike",
                           "icy touch", "plague strike", "death coil", "death strike", "heart strike", "scourge strike",
                           "obliterate", "frost strike", "mangle", "shred", "rip", "rake", "ferocious bite",
                           "swipe", "claw", "melee", "auto shot", "moonfire", "insect swarm", "holy fire",
                           "shadow word", "devouring plague", "vampiric touch"});
}

bool IsFocusPlayerAction(std::string const& name)
{
    std::string const n = ToLowerCopy(name);
    return ContainsAny(n, {"attack enemy player", "attack enemy flag carrier", "on enemy player", "enemy flag carrier"});
}

bool IsInstantPreferredAction(std::string const& name)
{
    return IsInterruptAction(name) || IsDefensiveAction(name) || IsCrowdControlAction(name) ||
           ContainsAny(ToLowerCopy(name), {"trinket", "vanish", "shadowstep", "blink", "disengage", "gift of the naaru"});
}

bool IsLoggableCombatAction(std::string const& name)
{
    if (name.empty() || IsMetaAction(name))
        return false;
    // Keep anything with a combat role flag, or generic non-meta combat actions.
    return IsInterruptAction(name) || IsHealActionName(name) || IsDefensiveAction(name) || IsCrowdControlAction(name) ||
           IsDamageAction(name) || IsFocusPlayerAction(name) || IsEnemyHealerAction(name);
}
}  // namespace CombatDecisionUtil
