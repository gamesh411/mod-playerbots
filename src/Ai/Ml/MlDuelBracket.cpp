/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "MlDuelBracket.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "AiFactory.h"
#include "ChatHelper.h"
#include "DBCEnums.h"
#include "DBCStores.h"
#include "Log.h"
#include "Map.h"
#include "MlDecisionLogger.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "Random.h"
#include "SpellDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "WorldSession.h"

namespace
{
bool IsRealPlayerParticipant(Player* p)
{
    if (!p)
        return true;
    PlayerbotAI* ai = GET_PLAYERBOT_AI(p);
    return !ai || ai->IsRealPlayer();
}

std::vector<std::string> SplitCsv(std::string const& s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
        if (c == sep)
        {
            if (!cur.empty())
                out.push_back(cur);
            cur.clear();
        }
        else if (c != ' ' && c != '\t')
            cur.push_back(c);
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

bool ParseSpecToken(std::string const& tok, MlDuelSpecKey& out)
{
    auto colon = tok.find(':');
    if (colon == std::string::npos)
        return false;
    out.cls = static_cast<uint8>(atoi(tok.substr(0, colon).c_str()));
    out.tab = static_cast<uint8>(atoi(tok.substr(colon + 1).c_str()));
    return out.cls > 0 && out.cls < MAX_CLASSES;
}
}  // namespace

MlDuelBracket& MlDuelBracket::instance()
{
    static MlDuelBracket inst;
    return inst;
}

void MlDuelBracket::LoadFromConfig()
{
    enabled = sPlayerbotAIConfig.mlDuelBracketEnabled;
    pairs.clear();
    allowedClassMask = sPlayerbotAIConfig.mlDuelBracketAllowedClassMask;
    maxMatchRange = sPlayerbotAIConfig.mlDuelBracketMaxMatchRange;
    rematchCooldownMs = sPlayerbotAIConfig.mlDuelBracketRematchCooldownMs;
    resetCooldownsOnDuelEnd = sPlayerbotAIConfig.mlDuelBracketResetCooldownsOnDuelEnd;

    if (!ParsePairs(sPlayerbotAIConfig.mlDuelBracketPairs))
    {
        // Default: Arms Warrior (1:0) vs Frost Mage (8:2)
        pairs.push_back({{CLASS_WARRIOR, 0}, {CLASS_MAGE, 2}});
    }

    if (!allowedClassMask)
    {
        for (MlDuelPair const& p : pairs)
        {
            allowedClassMask |= (1u << (p.a.cls - 1));
            allowedClassMask |= (1u << (p.b.cls - 1));
        }
    }

    if (!ParsePark(sPlayerbotAIConfig.mlDuelBracketParkAlliance, alliancePark))
    {
        // Elwynn map 34.00, 51.00 (outside SW gates) -> world -9120, 355.
        alliancePark = {0, -9120.f, 355.f, 93.2f, 0.7f};
    }
    if (!ParsePark(sPlayerbotAIConfig.mlDuelBracketParkHorde, hordePark))
    {
        // Durotar map 45.83, 13.90 -> world 1318.4, -4385.8.
        hordePark = {1, 1318.4f, -4385.8f, 26.5f, 3.5f};
    }

    {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        waiting.clear();
        lastDuelEndMs.clear();
        duelMatchIds.clear();
    }

    if (enabled)
    {
        LOG_INFO("playerbots",
                 "MlDuelBracket ON: {} pair(s), classMask=0x{:X}, resetCDs={}, parks A({} {:.0f},{:.0f}) H({} {:.0f},{:.0f})",
                 pairs.size(), allowedClassMask, resetCooldownsOnDuelEnd ? 1 : 0, alliancePark.mapId, alliancePark.x,
                 alliancePark.y, hordePark.mapId, hordePark.x, hordePark.y);
        ApplySpecProbOverrides();

        // DEC-027: dump Water Elemental Freeze targeting so S2 execute can claim pet-root combos.
        if (SpellInfo const* freeze = sSpellMgr->GetSpellInfo(33395))
        {
            LOG_INFO("playerbots",
                     "DEC-027 Freeze 33395 Targets=0x{:X} Explicit=0x{:X} Effect0A={} Effect0B={} NeedsUnit={}",
                     freeze->Targets, freeze->GetExplicitTargetMask(),
                     freeze->Effects[EFFECT_0].TargetA.GetTarget(), freeze->Effects[EFFECT_0].TargetB.GetTarget(),
                     freeze->NeedsExplicitUnitTarget() ? 1 : 0);
        }
        else
            LOG_ERROR("playerbots", "DEC-027 Freeze 33395 missing from SpellMgr");
    }
}

bool MlDuelBracket::ParsePairs(std::string const& raw)
{
    if (raw.empty())
        return false;

    for (std::string const& pairTok : SplitCsv(raw, ','))
    {
        auto dash = pairTok.find('-');
        if (dash == std::string::npos)
            continue;
        MlDuelPair p;
        if (!ParseSpecToken(pairTok.substr(0, dash), p.a) || !ParseSpecToken(pairTok.substr(dash + 1), p.b))
            continue;
        pairs.push_back(p);
    }
    return !pairs.empty();
}

bool MlDuelBracket::ParsePark(std::string const& raw, MlDuelPark& out)
{
    auto parts = SplitCsv(raw, ',');
    if (parts.size() < 4)
        return false;
    out.mapId = static_cast<uint32>(atoi(parts[0].c_str()));
    out.x = static_cast<float>(atof(parts[1].c_str()));
    out.y = static_cast<float>(atof(parts[2].c_str()));
    out.z = static_cast<float>(atof(parts[3].c_str()));
    out.o = parts.size() >= 5 ? static_cast<float>(atof(parts[4].c_str())) : 0.f;
    return true;
}

void MlDuelBracket::ApplySpecProbOverrides()
{
    // Zero all tabs for classes in the bracket, then set configured tabs to 100.
    std::unordered_map<uint8, std::vector<uint8>> tabsByClass;
    for (MlDuelPair const& p : pairs)
    {
        tabsByClass[p.a.cls].push_back(p.a.tab);
        tabsByClass[p.b.cls].push_back(p.b.tab);
    }

    for (auto const& kv : tabsByClass)
    {
        uint8 cls = kv.first;
        for (uint32 spec = 0; spec < MAX_SPECNO; ++spec)
            sPlayerbotAIConfig.randomClassSpecProb[cls][spec] = 0;
        for (uint8 tab : kv.second)
        {
            if (tab < MAX_SPECNO)
                sPlayerbotAIConfig.randomClassSpecProb[cls][tab] = 100;
        }
    }
}

bool MlDuelBracket::IsClassAllowed(uint8 cls) const
{
    if (!enabled || !allowedClassMask)
        return true;
    if (cls == 0 || cls >= MAX_CLASSES)
        return false;
    return (allowedClassMask & (1u << (cls - 1))) != 0;
}

MlDuelSpecKey MlDuelBracket::SpecOf(Player* bot) const
{
    MlDuelSpecKey key;
    if (!bot)
        return key;
    key.cls = bot->getClass();
    key.tab = AiFactory::GetPlayerSpecTab(bot);
    return key;
}

bool MlDuelBracket::IsBotEligibleSpec(Player* bot) const
{
    if (!enabled || !bot)
        return false;
    MlDuelSpecKey key = SpecOf(bot);
    for (MlDuelPair const& p : pairs)
        if (key == p.a || key == p.b)
            return true;
    return false;
}

bool MlDuelBracket::TryGetComplement(MlDuelSpecKey const& key, MlDuelSpecKey& outComplement) const
{
    for (MlDuelPair const& p : pairs)
    {
        if (key == p.a)
        {
            outComplement = p.b;
            return true;
        }
        if (key == p.b)
        {
            outComplement = p.a;
            return true;
        }
    }
    return false;
}

bool MlDuelBracket::AreaAllowsDuels(Player* bot) const
{
    if (!bot)
        return false;
    AreaTableEntry const* area = sAreaTableStore.LookupEntry(bot->GetAreaId());
    return area && (area->flags & AREA_FLAG_ALLOW_DUELS);
}

bool MlDuelBracket::IsNearPark(Player* bot) const
{
    if (!bot)
        return false;
    MlDuelPark const& park = (bot->GetTeamId() == TEAM_ALLIANCE) ? alliancePark : hordePark;
    if (bot->GetMapId() != park.mapId)
        return false;
    return bot->GetDistance2d(park.x, park.y) <= static_cast<float>(maxMatchRange) + 60.f;
}

void MlDuelBracket::EnsureUnmounted(Player* bot)
{
    if (!bot || !bot->IsInWorld() || !bot->IsMounted())
        return;
    if (bot->isMoving())
        bot->StopMoving();
    WorldPacket emptyPacket;
    bot->GetSession()->HandleCancelMountAuraOpcode(emptyPacket);
}

void MlDuelBracket::ForceToPark(Player* bot)
{
    if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
        return;
    if (bot->duel || bot->InBattleground() || bot->InArena())
        return;
    // Already in the duel zone: stay put. Re-scatter teleports caused mound hopping / flicker.
    if (IsNearPark(bot) && AreaAllowsDuels(bot))
        return;

    TeleportToPad(bot);
}

void MlDuelBracket::TeleportToPad(Player* bot, Position* outDest)
{
    if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
        return;
    if (bot->duel || bot->InBattleground() || bot->InArena())
        return;

    MlDuelPark const& park = (bot->GetTeamId() == TEAM_ALLIANCE) ? alliancePark : hordePark;
    // Mild scatter once on entry so the pad is not a single stack.
    float const scatter = 18.f;
    float jx = frand(-scatter, scatter);
    float jy = frand(-scatter, scatter);
    float x = park.x + jx;
    float y = park.y + jy;
    float z = park.z;
    if (bot->GetMapId() == park.mapId && bot->GetMap())
    {
        float ground = bot->GetMap()->GetHeight(bot->GetPhaseMask(), x, y, z + 40.f);
        if (ground > INVALID_HEIGHT)
            z = ground + 0.5f;
    }
    bot->TeleportTo(park.mapId, x, y, z, park.o);
    EnsureUnmounted(bot);
    if (outDest)
        outDest->Relocate(x, y, z, park.o);
}

bool MlDuelBracket::EnsureAtPark(Player* bot)
{
    if (!bot)
        return false;
    if (IsNearPark(bot) && AreaAllowsDuels(bot))
        return true;
    ForceToPark(bot);
    return true;
}

void MlDuelBracket::PatrolNearPark(Player* bot)
{
    if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported() || bot->duel)
        return;
    if (bot->isMoving() || bot->IsInCombat())
        return;
    if (!IsNearPark(bot) || !AreaAllowsDuels(bot))
    {
        ForceToPark(bot);
        return;
    }

    // Already pathing inside the pad — let it finish.
    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
        return;

    // Soft throttle so 200 idle bots do not all repath every AI tick.
    if (urand(1, 100) > 45)
        return;

    MlDuelPark const& park = (bot->GetTeamId() == TEAM_ALLIANCE) ? alliancePark : hordePark;
    float const radius = std::min(parkWanderRadius, static_cast<float>(maxMatchRange) * 0.45f);
    float const angle = frand(0.f, 6.2831853f);
    float const dist = frand(6.f, std::max(6.f, radius));
    float x = park.x + dist * std::cos(angle);
    float y = park.y + dist * std::sin(angle);
    float z = park.z;
    if (bot->GetMap())
    {
        float ground = bot->GetMap()->GetHeight(bot->GetPhaseMask(), x, y, z + 40.f);
        if (ground > INVALID_HEIGHT)
            z = ground + 0.5f;
    }

    bot->GetMotionMaster()->Clear();
    bot->GetMotionMaster()->MovePoint(1, x, y, z);
}

void MlDuelBracket::MoveTowardPartner(Player* bot, Player* partner)
{
    if (!bot || !partner || !bot->IsInWorld() || bot->IsBeingTeleported() || bot->duel)
        return;
    if (bot->isMoving() || bot->GetMapId() != partner->GetMapId())
        return;

    float const tx = partner->GetPositionX();
    float const ty = partner->GetPositionY();
    float const tz = partner->GetPositionZ();
    bot->GetMotionMaster()->Clear();
    bot->GetMotionMaster()->MovePoint(2, tx, ty, tz);
}

ObjectGuid MlDuelBracket::FindNearbyComplement(Player* bot, MlDuelSpecKey const& want) const
{
    if (!bot || !bot->GetMap())
        return ObjectGuid::Empty;

    float bestDist = static_cast<float>(maxMatchRange) + 25.f;
    ObjectGuid best;
    Map::PlayerList const& players = bot->GetMap()->GetPlayers();
    for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
    {
        Player* p = it->GetSource();
        if (!p || p == bot || !p->IsInWorld() || p->GetMapId() != bot->GetMapId())
            continue;
        if (!IsNearPark(p) || !AreaAllowsDuels(p))
            continue;
        PlayerbotAI* pAI = GET_PLAYERBOT_AI(p);
        if (!IsBracketCandidate(p, pAI))
            continue;
        if (!(SpecOf(p) == want))
            continue;
        float const d = bot->GetDistance(p);
        if (d < bestDist)
        {
            bestDist = d;
            best = p->GetGUID();
        }
    }
    return best;
}

bool MlDuelBracket::IsResourceReady(Player* bot) const
{
    if (!bot || !bot->IsAlive())
        return false;
    if (!bot->IsFullHealth())
        return false;

    auto powerFull = [&](Powers power) -> bool {
        uint32 const maxp = bot->GetMaxPower(power);
        return maxp == 0 || bot->GetPower(power) >= maxp;
    };

    // Gate on every fillable pool except Rage / Runic Power (and Rune slots).
    for (uint32 p = POWER_MANA; p < MAX_POWERS; ++p)
    {
        if (p == POWER_RAGE || p == POWER_RUNIC_POWER || p == POWER_RUNE)
            continue;
        if (!powerFull(Powers(p)))
            return false;
    }

    if (bot->getClass() == CLASS_DEATH_KNIGHT)
    {
        for (uint8 i = 0; i < MAX_RUNES; ++i)
            if (bot->GetRuneCooldown(i) > 0)
                return false;
    }

    return true;
}

void MlDuelBracket::RestoreForRematch(Player* bot)
{
    if (!bot)
        return;

    // Duel losers are usually at 1 HP, not dead; resurrect if a path killed them.
    if (!bot->IsAlive())
    {
        bot->ResurrectPlayer(1.0f);
        bot->SpawnCorpseBones();
    }

    bot->SetFullHealth();

    // Top every regenerative resource; zero the build-up pools (Rage / Runic Power) so a seat
    // cannot open the rematch with banked resources the other class has no equivalent of (DEC-037).
    for (uint32 p = POWER_MANA; p < MAX_POWERS; ++p)
    {
        if (p == POWER_RAGE || p == POWER_RUNIC_POWER)
        {
            bot->SetPower(Powers(p), 0);
            continue;
        }
        // Rune readiness is cleared below; POWER_RUNE is not a fillable pool.
        if (p == POWER_RUNE)
            continue;
        if (uint32 const maxp = bot->GetMaxPower(Powers(p)))
            bot->SetPower(Powers(p), maxp);
    }

    if (bot->getClass() == CLASS_DEATH_KNIGHT)
    {
        for (uint8 i = 0; i < MAX_RUNES; ++i)
            bot->SetRuneCooldown(i, 0);
    }

    if (resetCooldownsOnDuelEnd)
        bot->RemoveAllSpellCooldown();

    // Clear eat/drink (and sit) so rematch is not blocked by regen auras.
    bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_NOT_SEATED);
    if (bot->IsSitState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);

    // Lingering combat after a duel blocks rematch / new duel requests.
    if (bot->IsInCombat())
        bot->CombatStop(true);
}

bool MlDuelBracket::IsBracketCandidate(Player* bot, PlayerbotAI* botAI) const
{
    if (!enabled || !bot || !botAI)
        return false;
    if (!sRandomPlayerbotMgr.IsRandomBot(bot))
        return false;
    if (bot->InArena() || bot->InBattleground())
        return false;
    if (!bot->IsAlive() || bot->IsInCombat() || bot->duel)
        return false;
    if (!IsBotEligibleSpec(bot))
        return false;

    uint32 now = getMSTime();
    {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        auto it = lastDuelEndMs.find(bot->GetGUID().GetCounter());
        if (it != lastDuelEndMs.end() && getMSTimeDiff(it->second, now) < rematchCooldownMs)
            return false;
    }

    return true;
}

bool MlDuelBracket::IsIdleEligible(Player* bot, PlayerbotAI* botAI) const
{
    return IsBracketCandidate(bot, botAI) && IsResourceReady(bot);
}

bool MlDuelBracket::InitiateDuel(Player* challenger, Player* opponent)
{
    if (!challenger || !opponent)
        return false;
    if (challenger->duel || opponent->duel)
        return false;
    if (!AreaAllowsDuels(challenger) || !AreaAllowsDuels(opponent))
        return false;

    EnsureUnmounted(challenger);
    EnsureUnmounted(opponent);

    PlayerbotAI* ai = GET_PLAYERBOT_AI(challenger);
    if (!ai)
        return false;

    // Spell 7266 = Duel request
    std::ostringstream cmd;
    cmd << ChatHelper::FormatWorldobject(opponent) << " 7266";
    return ai->DoSpecificAction("cast custom spell", Event("ml duel bracket", cmd.str()), true);
}

bool MlDuelBracket::TryMatchOrQueue(PlayerbotAI* botAI)
{
    if (!enabled || !botAI)
        return false;

    Player* bot = botAI->GetBot();
    if (!IsBracketCandidate(bot, botAI))
        return false;

    EnsureAtPark(bot);
    RestoreForRematch(bot);

    // Self-heal wedged move flags: directional flags whose source is gone (executor detach
    // through death, aborted teleport) have no client to clear them, and they permanently
    // gate PatrolNearPark / MoveTowardPartner on isMoving().
    if (bot->isMoving() && bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE)
        bot->m_movementInfo.RemoveMovementFlag(MOVEMENTFLAG_FORWARD | MOVEMENTFLAG_BACKWARD |
                                               MOVEMENTFLAG_STRAFE_LEFT | MOVEMENTFLAG_STRAFE_RIGHT);

    if (!AreaAllowsDuels(bot) || !IsResourceReady(bot))
        return false;

    MlDuelSpecKey myKey = SpecOf(bot);
    MlDuelSpecKey want;
    if (!TryGetComplement(myKey, want))
        return false;

    uint32 now = getMSTime();
    uint32 const mapId = bot->GetMapId();
    ObjectGuid partnerGuid;

    {
        std::lock_guard<std::recursive_mutex> lock(mtx);

        // Only resolve Player* for waiters on *this* map. Alliance (EK) and Horde (Kalimdor)
        // parks run on different MapUpdater threads; reading/writing cross-map Player* under
        // the shared waitlist caused STATUS_HEAP_CORRUPTION (0xC0000374) under load.
        waiting.erase(std::remove_if(waiting.begin(), waiting.end(),
                                     [&](Waiter const& w) {
                                         // Keep self on the list; refreshed below if missing.
                                         if (w.guid == bot->GetGUID())
                                             return false;
                                         if (w.mapId != mapId)
                                         {
                                             // Time-expire other-map rows without touching their Player*.
                                             return getMSTimeDiff(w.queuedAtMs, now) > 120000;
                                         }
                                         Player* p = ObjectAccessor::FindPlayer(w.guid);
                                         return !p || !p->IsInWorld() || p->GetMapId() != mapId || p->duel ||
                                                p->InArena() || p->InBattleground();
                                     }),
                      waiting.end());

        for (auto it = waiting.begin(); it != waiting.end(); ++it)
        {
            if (!(it->key == want) || it->mapId != mapId)
                continue;
            Player* cand = ObjectAccessor::FindPlayer(it->guid);
            PlayerbotAI* candAI = cand ? GET_PLAYERBOT_AI(cand) : nullptr;
            if (!cand || cand->GetMapId() != mapId || !IsBracketCandidate(cand, candAI))
                continue;
            // Peek only — leave on waitlist until duel request actually fires.
            partnerGuid = cand->GetGUID();
            break;
        }

        // Always stay queued until InitiateDuel clears us (covers walk-into-range retries).
        bool present = false;
        for (Waiter const& w : waiting)
            if (w.guid == bot->GetGUID())
                present = true;
        if (!present)
            waiting.push_back({bot->GetGUID(), myKey, now, mapId});
    }

    // Waitlist miss: wander the pad and opportunistically pick a nearby complement.
    if (!partnerGuid)
    {
        PatrolNearPark(bot);
        partnerGuid = FindNearbyComplement(bot, want);
        if (!partnerGuid)
            return false;
    }

    Player* partner = ObjectAccessor::FindPlayer(partnerGuid);
    PlayerbotAI* partnerAI = partner ? GET_PLAYERBOT_AI(partner) : nullptr;
    if (!partner || partner->GetMapId() != mapId || !IsBracketCandidate(partner, partnerAI))
        return false;

    EnsureAtPark(bot);
    EnsureAtPark(partner);
    RestoreForRematch(bot);
    RestoreForRematch(partner);
    if (!AreaAllowsDuels(bot) || !AreaAllowsDuels(partner) || !IsResourceReady(bot) || !IsResourceReady(partner))
        return false;

    // Rematch re-anchoring: duel-chain drift (kite displacement per duel, rematches wherever the
    // last duel ended) decays pad density until pairing degenerates into long walk-ins. Snap a
    // drifted pair back to the pad between duels — invisible mid-rematch, keeps density bounded.
    float constexpr rematchAnchorYd = 60.f;
    MlDuelPark const& anchorPark = (bot->GetTeamId() == TEAM_ALLIANCE) ? alliancePark : hordePark;
    if (bot->GetDistance2d(anchorPark.x, anchorPark.y) > rematchAnchorYd ||
        partner->GetDistance2d(anchorPark.x, anchorPark.y) > rematchAnchorYd)
    {
        // Land the pair together at one pad point: independent scatter left them up to 36y
        // apart and cost an extra adjacent-teleport tick on every rematch.
        Position padDest;
        TeleportToPad(bot, &padDest);
        float const angle = frand(0.f, 6.2831853f);
        partner->TeleportTo(anchorPark.mapId, padDest.GetPositionX() + 3.f * std::cos(angle),
                            padDest.GetPositionY() + 3.f * std::sin(angle), padDest.GetPositionZ(),
                            partner->GetOrientation());
        EnsureUnmounted(partner);
        // Teleports resolve asynchronously; initiate on the next tick from the pad.
        return false;
    }

    if (bot->GetDistance(partner) > duelRequestRange)
    {
        // Kite-range duel endings leave pairs 15-30y apart at every rematch; walking that gap
        // dominated the rematch cycle (movement arm paid it every duel, melee endings never).
        // Teleport the partner adjacent instead — invisible between duels, uniform cycle time.
        float const angle = frand(0.f, 6.2831853f);
        float x = bot->GetPositionX() + 3.0f * std::cos(angle);
        float y = bot->GetPositionY() + 3.0f * std::sin(angle);
        float z = bot->GetPositionZ();
        if (bot->GetMap())
        {
            float const ground = bot->GetMap()->GetHeight(bot->GetPhaseMask(), x, y, z + 5.f);
            if (ground > INVALID_HEIGHT)
                z = ground + 0.5f;
        }
        partner->TeleportTo(bot->GetMapId(), x, y, z, partner->GetOrientation());
        return false;  // teleport resolves async; initiate next tick
    }

    ClearWaiting(bot->GetGUID());
    ClearWaiting(partner->GetGUID());

    // Lower guid initiates to avoid double-cast races.
    Player* challenger = bot->GetGUID().GetCounter() < partner->GetGUID().GetCounter() ? bot : partner;
    Player* opponent = challenger == bot ? partner : bot;
    return InitiateDuel(challenger, opponent);
}

void MlDuelBracket::ClearWaiting(ObjectGuid guid)
{
    std::lock_guard<std::recursive_mutex> lock(mtx);
    waiting.erase(std::remove_if(waiting.begin(), waiting.end(),
                                 [&](Waiter const& w) { return w.guid == guid; }),
                  waiting.end());
}

void MlDuelBracket::OnDuelStart(Player* p1, Player* p2)
{
    if (!enabled || !p1 || !p2)
        return;

    ClearWaiting(p1->GetGUID());
    ClearWaiting(p2->GetGUID());
    EnsureUnmounted(p1);
    EnsureUnmounted(p2);

    // Sparring vs a real player: keep unmount/wait cleanup, but do not register or log.
    // Farm bot-vs-bot CSV stays clean for DAgger.
    if (IsRealPlayerParticipant(p1) || IsRealPlayerParticipant(p2))
        return;

    uint32 matchId = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        matchId = nextDuelMatchId++;
        if (nextDuelMatchId == 0)
            nextDuelMatchId = 1;
        duelMatchIds[p1->GetGUID().GetCounter()] = matchId;
        duelMatchIds[p2->GetGUID().GetCounter()] = matchId;
        uint32 const matches = static_cast<uint32>(duelMatchIds.size() / 2);
        if (matches > peakMatchCount)
            peakMatchCount = matches;
    }
    sMlDecisionLogger.RegisterDuelMatch(p1->GetGUID(), p2->GetGUID(), matchId);
    sMlDecisionLogger.LogDuelStartSnapshot(p1, matchId);
    sMlDecisionLogger.LogDuelStartSnapshot(p2, matchId);
}

void MlDuelBracket::OnDuelEnd(Player* winner, Player* loser, DuelCompleteType /*type*/)
{
    if (!winner && !loser)
        return;

    uint32 now = getMSTime();
    ObjectGuid wGuid = winner ? winner->GetGUID() : ObjectGuid::Empty;
    ObjectGuid lGuid = loser ? loser->GetGUID() : ObjectGuid::Empty;

    {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (winner)
            lastDuelEndMs[winner->GetGUID().GetCounter()] = now;
        if (loser)
            lastDuelEndMs[loser->GetGUID().GetCounter()] = now;
        if (winner)
            duelMatchIds.erase(winner->GetGUID().GetCounter());
        if (loser)
            duelMatchIds.erase(loser->GetGUID().GetCounter());
    }

    if (winner)
        RestoreForRematch(winner);
    if (loser)
        RestoreForRematch(loser);
}

uint32 MlDuelBracket::GetTrackedMatchCount() const
{
    std::lock_guard<std::recursive_mutex> lock(mtx);
    return static_cast<uint32>(duelMatchIds.size() / 2);
}

uint32 MlDuelBracket::GetWaitingCount() const
{
    std::lock_guard<std::recursive_mutex> lock(mtx);
    return static_cast<uint32>(waiting.size());
}

void MlDuelBracket::PrintSaturation(uint32 onlineEligible, uint32 botsInDuel) const
{
    if (!enabled)
        return;

    uint32 const matches = GetTrackedMatchCount();
    uint32 waitingN = 0;
    uint32 waitWar = 0;
    uint32 waitMage = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        waitingN = static_cast<uint32>(waiting.size());
        for (Waiter const& w : waiting)
        {
            if (w.key.cls == CLASS_WARRIOR)
                ++waitWar;
            else if (w.key.cls == CLASS_MAGE)
                ++waitMage;
        }
    }

    float const satPct =
        onlineEligible > 0 ? (100.f * static_cast<float>(botsInDuel) / static_cast<float>(onlineEligible)) : 0.f;

    LOG_INFO("playerbots",
             "MlDuelBracket saturation: in_duel={} matches={} waiting={} (W={} M={}) eligible={} sat={:.1f}% "
             "peak_matches={}",
             botsInDuel, matches, waitingN, waitWar, waitMage, onlineEligible, satPct, peakMatchCount);
}
