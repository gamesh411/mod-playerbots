/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "CombatDecisionFeatures.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <sstream>

#include "AiFactory.h"
#include "MlDuelMovement.h"
#include "Player.h"
#include "Playerbots.h"
#include "ServerFacade.h"
#include "SharedDefines.h"
#include "SpellAuraDefines.h"

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

// Stable class one-hot slot (0..9). Returns -1 for unknown.
int ClassSlot(uint8 cls)
{
    switch (cls)
    {
        case CLASS_WARRIOR:
            return 0;
        case CLASS_PALADIN:
            return 1;
        case CLASS_HUNTER:
            return 2;
        case CLASS_ROGUE:
            return 3;
        case CLASS_PRIEST:
            return 4;
        case CLASS_DEATH_KNIGHT:
            return 5;
        case CLASS_SHAMAN:
            return 6;
        case CLASS_MAGE:
            return 7;
        case CLASS_WARLOCK:
            return 8;
        case CLASS_DRUID:
            return 9;
        default:
            return -1;
    }
}

void FillClassOneHot(uint8 cls, float* out10)
{
    for (int i = 0; i < 10; ++i)
        out10[i] = 0.0f;
    int slot = ClassSlot(cls);
    if (slot >= 0)
        out10[slot] = 1.0f;
}

void FillSpecOneHot(uint8 tab, float* out3)
{
    out3[0] = out3[1] = out3[2] = 0.0f;
    if (tab < 3)
        out3[tab] = 1.0f;
}

float DrRemaining(Unit* unit, DiminishingGroup group)
{
    if (!unit)
        return 1.0f;
    switch (unit->GetDiminishing(group))
    {
        case DIMINISHING_LEVEL_1:
            return 1.0f;
        case DIMINISHING_LEVEL_2:
            return 0.5f;
        case DIMINISHING_LEVEL_3:
            return 0.25f;
        case DIMINISHING_LEVEL_IMMUNE:
            return 0.0f;
        default:
            return 1.0f;
    }
}

// 1 if any known spell is off cooldown; 0 if all known are on CD; 0 if none known.
float AnySpellReady(Player* player, std::initializer_list<uint32> spellIds)
{
    if (!player)
        return 0.0f;
    bool anyKnown = false;
    for (uint32 id : spellIds)
    {
        if (!player->HasSpell(id))
            continue;
        anyKnown = true;
        if (!player->HasSpellCooldown(id))
            return 1.0f;
    }
    return anyKnown ? 0.0f : 0.0f;
}

bool HasImmunityAura(Unit* unit)
{
    if (!unit)
        return false;
    // Ice Block, Divine Shield, Cloak of Shadows, Cyclone, Banish, etc.
    if (unit->HasAura(45438) || unit->HasAura(642) || unit->HasAura(31224) || unit->HasAura(33786) ||
        unit->HasAura(18647))
        return true;
    if (unit->HasAuraType(SPELL_AURA_SCHOOL_IMMUNITY) || unit->HasAuraType(SPELL_AURA_MECHANIC_IMMUNITY) ||
        unit->HasAuraType(SPELL_AURA_DAMAGE_IMMUNITY))
        return true;
    return false;
}

float UnitPowerFrac(Unit* unit)
{
    if (!unit)
        return 0.0f;
    Powers pt = unit->getPowerType();
    uint32 maxp = unit->GetMaxPower(pt);
    if (!maxp)
        return 0.0f;
    return static_cast<float>(unit->GetPower(pt)) / static_cast<float>(maxp);
}

void FillRoleCds(Player* player, float& kick, float& defensive, float& offensive, float& gap, float& root,
                 float& trinket)
{
    kick = defensive = offensive = gap = root = trinket = 0.0f;
    if (!player)
        return;

    trinket = AnySpellReady(player, {42292, 59752});  // PvP trinket / EMFH

    switch (player->getClass())
    {
        case CLASS_WARRIOR:
            kick = AnySpellReady(player, {6552, 72});  // Pummel, Shield Bash
            defensive = AnySpellReady(player, {871, 12975, 23920, 2565});  // SW, LS, Reflect, Block
            offensive = AnySpellReady(player, {1719, 20230, 46924, 12292});  // Reck, Retal, BS, DW
            gap = AnySpellReady(player, {11578, 100, 20252, 3411});  // Charge ranks, Intercept, Intervene
            root = AnySpellReady(player, {5246, 676});  // Intimidating Shout, Disarm (pressure CDs)
            break;
        case CLASS_MAGE:
            kick = AnySpellReady(player, {2139});
            defensive = AnySpellReady(player, {45438, 11958});  // Ice Block, Cold Snap
            offensive = AnySpellReady(player, {12472, 12043, 12042, 11129, 44572, 55342});
            gap = AnySpellReady(player, {1953});  // Blink
            root = AnySpellReady(player, {42917, 27088, 10285, 6131, 865, 122, 33395});  // Frost Nova ranks + Freeze
            break;
        case CLASS_ROGUE:
            kick = AnySpellReady(player, {1766, 1776});
            defensive = AnySpellReady(player, {31224, 5277, 1856});
            offensive = AnySpellReady(player, {13750, 51690, 14177, 13877});
            gap = AnySpellReady(player, {36554, 14185});
            root = AnySpellReady(player, {2094, 1776, 408, 1833});
            break;
        case CLASS_PRIEST:
            kick = AnySpellReady(player, {15487});
            defensive = AnySpellReady(player, {47585, 33206, 47788});
            offensive = AnySpellReady(player, {10060, 34433, 14751});
            gap = 0.0f;
            root = AnySpellReady(player, {8122, 64044});
            break;
        case CLASS_PALADIN:
            kick = AnySpellReady(player, {10308, 853});  // HoJ ranks
            defensive = AnySpellReady(player, {642, 498, 10278, 1044});
            offensive = AnySpellReady(player, {31884, 20066});
            gap = 0.0f;
            root = AnySpellReady(player, {10308, 853, 20066});
            break;
        case CLASS_HUNTER:
            kick = AnySpellReady(player, {34490, 19244, 19647});
            defensive = AnySpellReady(player, {19263, 5384, 781});
            offensive = AnySpellReady(player, {19574, 3045, 23989});
            gap = AnySpellReady(player, {781, 5116});
            root = AnySpellReady(player, {60192, 14311, 1499, 13809});
            break;
        case CLASS_WARLOCK:
            kick = AnySpellReady(player, {19647, 19244});  // pet spell lock often on pet; best-effort
            defensive = AnySpellReady(player, {48020, 47883, 1122});
            offensive = AnySpellReady(player, {17962, 59164, 47241});
            gap = AnySpellReady(player, {48020});
            root = AnySpellReady(player, {17928, 6215, 5484, 6789});
            break;
        case CLASS_SHAMAN:
            kick = AnySpellReady(player, {57994});
            defensive = AnySpellReady(player, {30823, 16188, 8177});
            offensive = AnySpellReady(player, {16166, 2894, 51533});
            gap = AnySpellReady(player, {58875});
            root = AnySpellReady(player, {51514, 63685, 64695});
            break;
        case CLASS_DEATH_KNIGHT:
            kick = AnySpellReady(player, {47528});
            defensive = AnySpellReady(player, {48707, 48792, 55233, 49039});
            offensive = AnySpellReady(player, {49016, 51271, 49206, 47568});
            gap = AnySpellReady(player, {47476, 49576, 46584});
            root = AnySpellReady(player, {47476, 45524, 49203});
            break;
        case CLASS_DRUID:
            kick = AnySpellReady(player, {8983, 5211, 22570});
            defensive = AnySpellReady(player, {61336, 22812, 22842, 29166});
            offensive = AnySpellReady(player, {50334, 48505, 33831, 17116});
            gap = AnySpellReady(player, {49376, 16979});
            root = AnySpellReady(player, {53308, 26989, 339, 33786, 2637});
            break;
        default:
            break;
    }
}
}  // namespace

CombatFeatureVector CombatDecisionFeaturesValue::Calculate()
{
    CombatFeatureVector features{};
    features.fill(0.0f);

    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsInWorld())
        return features;

    // --- Core ---
    features[CF_SELF_HEALTH] = AI_VALUE2(uint8, "health", "self target") / 100.0f;
    features[CF_SELF_MANA] = AI_VALUE2(bool, "has mana", "self target")
                                 ? AI_VALUE2(uint8, "mana", "self target") / 100.0f
                                 : UnitPowerFrac(bot);

    Unit* target = AI_VALUE(Unit*, "current target");
    Unit* foe = nullptr;
    if (bot->duel && bot->duel->Opponent)
        foe = bot->duel->Opponent;
    else if (target && target->IsPlayer())
        foe = target;
    else
        foe = target;

    if (foe && foe->IsAlive())
    {
        features[CF_TARGET_HEALTH] = foe->GetHealthPct() / 100.0f;
        features[CF_TARGET_IS_PLAYER] = foe->IsPlayer() ? 1.0f : 0.0f;
        features[CF_TARGET_IS_CASTING] = foe->IsNonMeleeSpellCast(false) ? 1.0f : 0.0f;
        if (UnitCastingPositive(foe))
            features[CF_HAS_ENEMY_HEALER] = 1.0f;
    }
    else if (target && target->IsAlive())
    {
        features[CF_TARGET_HEALTH] = target->GetHealthPct() / 100.0f;
        features[CF_TARGET_IS_PLAYER] = target->IsPlayer() ? 1.0f : 0.0f;
        features[CF_TARGET_IS_CASTING] = target->IsNonMeleeSpellCast(false) ? 1.0f : 0.0f;
        if (UnitCastingPositive(target))
            features[CF_HAS_ENEMY_HEALER] = 1.0f;
    }

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

    // --- Pack Class ---
    {
        float selfClass[10];
        float selfSpec[3];
        FillClassOneHot(bot->getClass(), selfClass);
        FillSpecOneHot(AiFactory::GetPlayerSpecTab(bot), selfSpec);
        for (int i = 0; i < 10; ++i)
            features[CF_SELF_CLASS_0 + i] = selfClass[i];
        features[CF_SELF_SPEC_0] = selfSpec[0];
        features[CF_SELF_SPEC_1] = selfSpec[1];
        features[CF_SELF_SPEC_2] = selfSpec[2];

        if (Player* foePlayer = foe ? foe->ToPlayer() : nullptr)
        {
            float foeClass[10];
            float foeSpec[3];
            FillClassOneHot(foePlayer->getClass(), foeClass);
            FillSpecOneHot(AiFactory::GetPlayerSpecTab(foePlayer), foeSpec);
            for (int i = 0; i < 10; ++i)
                features[CF_FOE_CLASS_0 + i] = foeClass[i];
            features[CF_FOE_SPEC_0] = foeSpec[0];
            features[CF_FOE_SPEC_1] = foeSpec[1];
            features[CF_FOE_SPEC_2] = foeSpec[2];
        }
    }

    // --- Pack DuelCD (self + foe; private-server omniscience for training) ---
    {
        float kick, def, off, gap, root, trinket;
        FillRoleCds(bot, kick, def, off, gap, root, trinket);
        features[CF_SELF_KICK_READY] = kick;
        features[CF_SELF_DEFENSIVE_READY] = def;
        features[CF_SELF_OFFENSIVE_CD_READY] = off;
        features[CF_SELF_GAPCLOSE_READY] = gap;
        features[CF_SELF_TRINKET_READY] = trinket;

        if (Player* foePlayer = foe ? foe->ToPlayer() : nullptr)
        {
            FillRoleCds(foePlayer, kick, def, off, gap, root, trinket);
            features[CF_FOE_KICK_READY] = kick;
            features[CF_FOE_DEFENSIVE_READY] = def;
            features[CF_FOE_OFFENSIVE_CD_READY] = off;
            features[CF_FOE_GAPCLOSE_READY] = gap;
            features[CF_FOE_ROOT_READY] = root;
            features[CF_FOE_TRINKET_READY] = trinket;
        }
    }

    // --- Pack DuelDR ---
    features[CF_SELF_DR_STUN] = DrRemaining(bot, DIMINISHING_CONTROLLED_STUN);
    features[CF_SELF_DR_SILENCE] = DrRemaining(bot, DIMINISHING_SILENCE);
    features[CF_SELF_DR_ROOT] = DrRemaining(bot, DIMINISHING_CONTROLLED_ROOT);
    features[CF_SELF_DR_DISORIENT] = DrRemaining(bot, DIMINISHING_DISORIENT);
    features[CF_SELF_DR_FEAR] = DrRemaining(bot, DIMINISHING_FEAR);
    features[CF_SELF_HAS_IMMUNITY] = HasImmunityAura(bot) ? 1.0f : 0.0f;

    if (foe)
    {
        features[CF_FOE_DR_STUN] = DrRemaining(foe, DIMINISHING_CONTROLLED_STUN);
        features[CF_FOE_DR_SILENCE] = DrRemaining(foe, DIMINISHING_SILENCE);
        features[CF_FOE_DR_ROOT] = DrRemaining(foe, DIMINISHING_CONTROLLED_ROOT);
        features[CF_FOE_DR_DISORIENT] = DrRemaining(foe, DIMINISHING_DISORIENT);
        features[CF_FOE_DR_FEAR] = DrRemaining(foe, DIMINISHING_FEAR);
        features[CF_FOE_HAS_IMMUNITY] = HasImmunityAura(foe) ? 1.0f : 0.0f;
        features[CF_FOE_POWER] = UnitPowerFrac(foe);
        features[CF_FOE_HAS_CONTROL_LOSS] = foe->HasUnitState(UNIT_STATE_LOST_CONTROL) ? 1.0f : 0.0f;
    }

    // --- Pack DuelRange ---
    if (foe && foe->IsAlive() && foe->IsInWorld() && foe->GetMapId() == bot->GetMapId())
    {
        float dist = bot->GetDistance(foe);
        features[CF_DIST_NORM] = std::clamp(dist / 40.0f, 0.0f, 1.0f);
        features[CF_IN_MELEE] = bot->IsWithinMeleeRange(foe) ? 1.0f : 0.0f;
        features[CF_IN_LOS] = bot->IsWithinLOSInMap(foe) ? 1.0f : 0.0f;
        features[CF_SELF_FACING_FOE] = bot->HasInArc(static_cast<float>(M_PI), foe) ? 1.0f : 0.0f;
        features[CF_FOE_FACING_SELF] = foe->HasInArc(static_cast<float>(M_PI), bot) ? 1.0f : 0.0f;
        features[CF_BEHIND_FOE] = !foe->HasInArc(static_cast<float>(M_PI), bot) ? 1.0f : 0.0f;
    }

    features[CF_IN_DUEL] = (bot->duel && bot->duel->Opponent) ? 1.0f : 0.0f;

    // --- Pack CF_MOVE (DEC-036): kinematics, impairment, walkability probes ---
    if (foe && foe->IsAlive() && foe->IsInWorld() && foe->GetMapId() == bot->GetMapId())
    {
        constexpr float baseRun = 7.0f;  // playerBaseMoveSpeed[MOVE_RUN]
        auto normalizeRel = [](float a)
        {
            while (a > float(M_PI))
                a -= 2.0f * float(M_PI);
            while (a < -float(M_PI))
                a += 2.0f * float(M_PI);
            return a / float(M_PI);  // [-1, 1]
        };
        // Movement direction from client move flags in the unit's facing frame (x fwd, y left).
        auto moveDir = [](Unit* u, bool& moving)
        {
            uint32 const f = u->m_movementInfo.GetMovementFlags();
            float dx = 0.0f, dy = 0.0f;
            if (f & MOVEMENTFLAG_FORWARD)
                dx += 1.0f;
            if (f & MOVEMENTFLAG_BACKWARD)
                dx -= 1.0f;
            if (f & MOVEMENTFLAG_STRAFE_LEFT)
                dy += 1.0f;
            if (f & MOVEMENTFLAG_STRAFE_RIGHT)
                dy -= 1.0f;
            moving = (dx != 0.0f || dy != 0.0f);
            return moving ? u->GetOrientation() + std::atan2(dy, dx) : u->GetOrientation();
        };

        float const bearingToFoe = bot->GetAngle(foe);
        float const bearingToSelf = foe->GetAngle(bot);

        bool selfMoving = false, foeMoving = false;
        float const selfMoveDir = moveDir(bot, selfMoving);
        float const foeMoveDir = moveDir(foe, foeMoving);
        float const selfSpeed = selfMoving ? bot->GetSpeed(bot->m_movementInfo.GetSpeedType()) : 0.0f;
        float const foeSpeed = foeMoving ? foe->GetSpeed(foe->m_movementInfo.GetSpeedType()) : 0.0f;

        features[CF_MOVE_SELF_SPEED_FRAC] = std::clamp(selfSpeed / baseRun, 0.0f, 2.0f);
        features[CF_MOVE_SELF_HEADING_REL] = selfMoving ? normalizeRel(selfMoveDir - bearingToFoe) : 0.0f;
        features[CF_MOVE_FOE_SPEED_FRAC] = std::clamp(foeSpeed / baseRun, 0.0f, 2.0f);
        features[CF_MOVE_FOE_HEADING_REL] = foeMoving ? normalizeRel(foeMoveDir - bearingToSelf) : 0.0f;
        features[CF_MOVE_SELF_FACING_OFFSET] = normalizeRel(bot->GetOrientation() - bearingToFoe);
        features[CF_MOVE_FOE_FACING_OFFSET] = normalizeRel(foe->GetOrientation() - bearingToSelf);

        // Closing speed: sum of each side's velocity component along the separation axis.
        float closing = 0.0f;
        if (selfMoving)
            closing += selfSpeed * std::cos(selfMoveDir - bearingToFoe);
        if (foeMoving)
            closing += foeSpeed * std::cos(foeMoveDir - bearingToSelf);
        features[CF_MOVE_CLOSING_SPEED] = std::clamp(closing / baseRun, -2.0f, 2.0f);

        features[CF_MOVE_SELF_AIRBORNE] =
            bot->m_movementInfo.HasMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR) ? 1.0f : 0.0f;
        features[CF_MOVE_SELF_SNARE_FRAC] = std::clamp(1.0f - bot->GetSpeed(MOVE_RUN) / baseRun, 0.0f, 1.0f);
        features[CF_MOVE_SELF_ROOTED] =
            (bot->IsRooted() || bot->HasUnitState(UNIT_STATE_ROOT)) ? 1.0f : 0.0f;
        features[CF_MOVE_FOE_SNARE_FRAC] = std::clamp(1.0f - foe->GetSpeed(MOVE_RUN) / baseRun, 0.0f, 1.0f);
        features[CF_MOVE_FOE_ROOTED] =
            (foe->IsRooted() || foe->HasUnitState(UNIT_STATE_ROOT)) ? 1.0f : 0.0f;

        float probes[8];
        sMlDuelMovement.GetProbes(bot, foe, probes);
        for (int k = 0; k < 8; ++k)
            features[CF_MOVE_PROBE_N + k] = probes[k];
    }

    return features;
}

std::string const CombatDecisionFeaturesValue::Format()
{
    CombatFeatureVector const& f = value;
    std::ostringstream out;
    out << "hp=" << f[CF_SELF_HEALTH] << " tgtHp=" << f[CF_TARGET_HEALTH] << " cast=" << f[CF_TARGET_IS_CASTING]
        << " dist=" << f[CF_DIST_NORM] << " melee=" << f[CF_IN_MELEE] << " kick=" << f[CF_SELF_KICK_READY]
        << " foeKick=" << f[CF_FOE_KICK_READY] << " foeIB=" << f[CF_FOE_HAS_IMMUNITY]
        << " duel=" << f[CF_IN_DUEL];
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
                        "duel_start",
                        "unstealth", "set behind", "set pet", "toggle pet", "cast greater blessing assignment",
                        "select new target", "update strategy", "chat", "emote", "rpg ", "travel", "grind", "loot",
                        "add all loot", "equip", "use stone", "use oil", "wait for", "guard", "stay", "follow master",
                        // Duel-bracket logistics / talent swap — not combat decisions for the ranker.
                        "ml duel bracket", "accept duel", "activate primary spec", "activate secondary spec"});
}

bool IsInterruptAction(std::string const& name)
{
    std::string const n = ToLowerCopy(name);
    return ContainsAny(n, {"kick", "pummel", "counterspell", "mind freeze", "wind shear", "spell lock", "shield bash",
                           "strangulate", "silencing shot", "arcane torrent", "deadly throw", "gouge", "silence",
                           "bash"});
}

bool IsEnemyHealerAction(std::string const& name)
{
    std::string const n = ToLowerCopy(name);
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
    return IsInterruptAction(name) || IsHealActionName(name) || IsDefensiveAction(name) || IsCrowdControlAction(name) ||
           IsDamageAction(name) || IsFocusPlayerAction(name) || IsEnemyHealerAction(name);
}
}  // namespace CombatDecisionUtil
