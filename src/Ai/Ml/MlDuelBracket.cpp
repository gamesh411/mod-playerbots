/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "MlDuelBracket.h"

#include <algorithm>
#include <sstream>

#include "AiFactory.h"
#include "ChatHelper.h"
#include "DBCEnums.h"
#include "DBCStores.h"
#include "Log.h"
#include "MlDecisionLogger.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "Random.h"
#include "SpellDefines.h"
#include "Timer.h"

namespace
{
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
        // Elwynn Forest, east of Stormwind (duel-allowed outdoors)
        alliancePark = {0, -9104.f, 416.f, 92.5f, 0.7f};
    }
    if (!ParsePark(sPlayerbotAIConfig.mlDuelBracketParkHorde, hordePark))
    {
        // Durotar, south of Orgrimmar
        hordePark = {1, 1357.f, -4369.f, 26.5f, 3.5f};
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
                 "MlDuelBracket ON: {} pair(s), classMask=0x{:X}, parks A({} {:.0f},{:.0f}) H({} {:.0f},{:.0f})",
                 pairs.size(), allowedClassMask, alliancePark.mapId, alliancePark.x, alliancePark.y, hordePark.mapId,
                 hordePark.x, hordePark.y);
        ApplySpecProbOverrides();
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
    return bot->GetDistance2d(park.x, park.y) <= static_cast<float>(maxMatchRange) + 20.f;
}

void MlDuelBracket::ForceToPark(Player* bot)
{
    if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
        return;
    if (bot->duel || bot->InBattleground() || bot->InArena())
        return;

    MlDuelPark const& park = (bot->GetTeamId() == TEAM_ALLIANCE) ? alliancePark : hordePark;
    // Small jitter so bots don't stack on one point.
    float jx = frand(-12.f, 12.f);
    float jy = frand(-12.f, 12.f);
    bot->TeleportTo(park.mapId, park.x + jx, park.y + jy, park.z, park.o);
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

    // DEC-024: Mana / Energy / Focus gated; Rage + Runic Power ungated.
    if (!powerFull(POWER_MANA) || !powerFull(POWER_ENERGY) || !powerFull(POWER_FOCUS))
        return false;

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
    if (!bot || !bot->IsAlive())
        return;

    bot->SetFullHealth();

    if (uint32 const maxMana = bot->GetMaxPower(POWER_MANA))
        bot->SetPower(POWER_MANA, maxMana);
    if (uint32 const maxEnergy = bot->GetMaxPower(POWER_ENERGY))
        bot->SetPower(POWER_ENERGY, maxEnergy);
    if (uint32 const maxFocus = bot->GetMaxPower(POWER_FOCUS))
        bot->SetPower(POWER_FOCUS, maxFocus);

    if (bot->getClass() == CLASS_DEATH_KNIGHT)
    {
        for (uint8 i = 0; i < MAX_RUNES; ++i)
            bot->SetRuneCooldown(i, 0);
    }

    // Clear eat/drink (and sit) so rematch is not blocked by regen auras.
    bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_NOT_SEATED);
    if (bot->IsSitState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);
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
    if (!AreaAllowsDuels(bot) || !IsResourceReady(bot))
        return false;

    MlDuelSpecKey myKey = SpecOf(bot);
    MlDuelSpecKey want;
    if (!TryGetComplement(myKey, want))
        return false;

    uint32 now = getMSTime();
    Player* partner = nullptr;

    {
        std::lock_guard<std::recursive_mutex> lock(mtx);

        // Drop stale / invalid waiters
        waiting.erase(std::remove_if(waiting.begin(), waiting.end(),
                                     [&](Waiter const& w) {
                                         if (w.guid == bot->GetGUID())
                                             return true;
                                         Player* p = ObjectAccessor::FindPlayer(w.guid);
                                         return !p || !p->IsInWorld() || p->duel || p->InArena() || p->InBattleground();
                                     }),
                      waiting.end());

        for (auto it = waiting.begin(); it != waiting.end(); ++it)
        {
            if (!(it->key == want))
                continue;
            Player* cand = ObjectAccessor::FindPlayer(it->guid);
            PlayerbotAI* candAI = cand ? GET_PLAYERBOT_AI(cand) : nullptr;
            if (!cand || !IsBracketCandidate(cand, candAI))
                continue;
            EnsureAtPark(cand);
            RestoreForRematch(cand);
            if (!IsResourceReady(cand))
                continue;
            // Same faction preferred for open-world duel flag; allow cross-faction if both at park.
            if (cand->GetMapId() != bot->GetMapId())
                continue;
            if (bot->GetDistance(cand) > static_cast<float>(maxMatchRange) + 40.f)
            {
                // Pull partner to our park pad.
                EnsureAtPark(cand);
            }
            partner = cand;
            waiting.erase(it);
            break;
        }

        if (!partner)
        {
            // Already waiting?
            bool present = false;
            for (Waiter const& w : waiting)
                if (w.guid == bot->GetGUID())
                    present = true;
            if (!present)
                waiting.push_back({bot->GetGUID(), myKey, now});
            return false;
        }
    }

    // Lower guid initiates to avoid double-cast races.
    Player* challenger = bot->GetGUID().GetCounter() < partner->GetGUID().GetCounter() ? bot : partner;
    Player* opponent = challenger == bot ? partner : bot;
    EnsureAtPark(challenger);
    EnsureAtPark(opponent);
    RestoreForRematch(challenger);
    RestoreForRematch(opponent);
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

    uint32 matchId = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        matchId = nextDuelMatchId++;
        if (nextDuelMatchId == 0)
            nextDuelMatchId = 1;
        duelMatchIds[p1->GetGUID().GetCounter()] = matchId;
        duelMatchIds[p2->GetGUID().GetCounter()] = matchId;
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

}
