/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "MlDecisionLogger.h"

#include <fstream>
#include <limits>

#include "Battleground.h"
#include "CombatDecisionFeatures.h"
#include "HeuristicScores.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "Timer.h"

MlDecisionLogger& MlDecisionLogger::instance()
{
    static MlDecisionLogger inst;
    return inst;
}

void MlDecisionLogger::OnActionExecuted(PlayerbotAI* botAI, std::string const& actionName, float heuristicScore,
                                        float finalScore, bool explored)
{
    if (!sPlayerbotAIConfig.mlLoggingEnabled || !botAI)
        return;

    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsInWorld())
        return;

    // Skip navigation / maintenance noise (phase A).
    // Duels (DEC-014): log any non-meta spellbook ability name, not just substring-matched roles.
    bool inDuel = bot->duel && bot->duel->Opponent;
    if (inDuel)
    {
        if (actionName.empty() || CombatDecisionUtil::IsMetaAction(actionName))
            return;
    }
    else if (!CombatDecisionUtil::IsLoggableCombatAction(actionName))
        return;

    // Universal PvE + PvP: BG/arena/duel, mastered bots, or anyone currently in combat.
    // in_bg / in_arena / in_duel columns keep the activity-zone distinction for separate trainers.
    bool interesting = botAI->HasRealPlayerMaster() || bot->InBattleground() || bot->InArena() || inDuel ||
                       bot->IsInCombat();
    if (!interesting && !sPlayerbotAIConfig.mlLogAllBots)
        return;

    AiObjectContext* context = botAI->GetAiObjectContext();
    if (!context)
        return;

    CombatFeatureVector features = AI_VALUE(CombatFeatureVector, "combat decision features");

    MlPendingDecision d;
    d.episodeId = nextEpisodeId++;
    d.matchId = 0;
    d.inDuel = inDuel;
    if (Battleground* bg = bot->GetBattleground())
        d.matchId = bg->GetInstanceID();
    else if (inDuel)
    {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = duelMatchByGuid.find(bot->GetGUID().GetCounter());
        if (it != duelMatchByGuid.end())
            d.matchId = it->second;
    }
    d.botGuid = bot->GetGUID();
    d.logTimeMs = getMSTime();
    d.resolveAtMs = d.logTimeMs + sPlayerbotAIConfig.mlRewardDelayMs;
    d.features = features;
    d.actionName = actionName;
    d.heuristicScore = heuristicScore;
    d.finalScore = finalScore;
    d.selfHpAtLog = static_cast<uint8>(features[CF_SELF_HEALTH] * 100.0f);
    d.targetHpAtLog = static_cast<uint8>(features[CF_TARGET_HEALTH] * 100.0f);
    d.targetWasCasting = features[CF_TARGET_IS_CASTING] > 0.5f;
    d.targetWasHealing = features[CF_HAS_ENEMY_HEALER] > 0.5f;
    d.wasInterruptAction = CombatDecisionUtil::IsInterruptAction(actionName);
    d.inArena = bot->InArena();
    d.inBg = bot->InBattleground();
    d.explored = explored;

    std::lock_guard<std::mutex> lock(mtx);
    while (pending.size() > 5000)
        pending.pop_front();
    pending.push_back(d);
}

float MlDecisionLogger::ComputeReward(PlayerbotAI* botAI, MlPendingDecision const& d) const
{
    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsAlive())
        return d.inDuel ? 0.0f : -2.0f;  // duel: death is scored by terminal loss, not short panic

    AiObjectContext* context = botAI->GetAiObjectContext();
    if (!context)
        return 0.0f;

    Unit* target = AI_VALUE(Unit*, "current target");
    if (d.inDuel)
    {
        // DEC-016: win at any cost — short reward only pressures the opponent.
        // No self-HP survival shaping (that teaches turtling). Terminal λ dominates.
        Unit* foe = bot->duel && bot->duel->Opponent ? bot->duel->Opponent : target;
        uint8 foeHpNow = foe && foe->IsAlive() ? static_cast<uint8>(foe->GetHealthPct()) : 0;
        float reward = 0.0f;
        if (foe && foe->IsAlive() && foeHpNow + 10 < d.targetHpAtLog)
            reward += 0.5f;
        if (foe && !foe->IsAlive())
            reward += 1.0f;
        if (d.wasInterruptAction && d.targetWasCasting)
        {
            bool stillCasting = foe && foe->IsNonMeleeSpellCast(false);
            reward += stillCasting ? -0.25f : 0.75f;
        }
        // Keep |short| << MlDuelTerminalLambda so win/loss is always the dominant signal.
        if (reward > 2.0f)
            reward = 2.0f;
        if (reward < -2.0f)
            reward = -2.0f;
        return reward;
    }

    float reward = 0.0f;
    float survival = 0.0f;
    uint8 selfHpNow = AI_VALUE2(uint8, "health", "self target");
    uint8 targetHpNow = target && target->IsAlive() ? static_cast<uint8>(target->GetHealthPct()) : 0;

    // Survival matters but must not dominate: small alive bonus, keep HP swing signals.
    if (selfHpNow > 0)
        survival += 0.05f;
    if (selfHpNow + 15 < d.selfHpAtLog)
        survival -= 0.8f;
    if (selfHpNow > d.selfHpAtLog + 10)
        survival += 0.5f;

    // Pressure on target
    if (target && target->IsAlive() && targetHpNow + 10 < d.targetHpAtLog)
        reward += 0.6f;
    if (target && !target->IsAlive())
        reward += 2.0f;

    // Interrupt success: spell-agnostic (any interrupt vs any cast; extra if it was a heal cast).
    if (d.wasInterruptAction && d.targetWasCasting)
    {
        bool stillCasting = target && target->IsNonMeleeSpellCast(false);
        float interruptReward = stillCasting ? -1.0f : 1.5f;
        if (!stillCasting && d.targetWasHealing)
            interruptReward += 0.5f;  // stopping a heal is especially good
        reward += interruptReward;
    }

    CombatFeatureVector now = AI_VALUE(CombatFeatureVector, "combat decision features");
    if (d.features[CF_HAS_ENEMY_HEALER] > 0.5f && now[CF_HAS_ENEMY_HEALER] < 0.5f)
        reward += 0.7f;

    reward += survival;

    // Mild PvP activity boost (BG/arena), not a second survival multiplier.
    if (d.inArena || d.inBg)
        reward *= 1.15f;

    return reward;
}

void MlDecisionLogger::WriteRow(MlPendingDecision const& d, float reward, float terminal)
{
    std::string path = sPlayerbotAIConfig.mlLogFile;
    bool duelFile = false;
    if (d.inDuel && !sPlayerbotAIConfig.mlDuelBracketLogFile.empty())
    {
        path = sPlayerbotAIConfig.mlDuelBracketLogFile;
        duelFile = true;
    }
    if (path.empty())
        return;

    float flags[AF_COUNT];
    HeuristicScores::FillActionFlags(d.actionName, flags);

    std::string action = d.actionName;
    for (char& c : action)
        if (c == ',')
            c = ';';

    std::lock_guard<std::mutex> fileLock(sPlayerbotAIConfig.m_logMtx);
    std::ofstream out(path.c_str(), std::ios::app);
    if (!out)
        return;

    bool& wroteHeader = duelFile ? duelHeaderWritten : headerWritten;
    if (!wroteHeader)
    {
        // Restart-safe: appending to an existing file must not rewrite the header mid-CSV.
        bool fileHasContent = false;
        {
            std::ifstream probe(path.c_str(), std::ios::binary | std::ios::ate);
            fileHasContent = probe.good() && probe.tellg() > 0;
        }
        if (!fileHasContent)
        {
            if (duelFile)
            {
                out << "episode_id,match_id,bot_guid,time_ms,action,reward,short_reward,terminal,explored,heuristic,final_score,"
                       "in_bg,in_arena,in_duel";
            }
            else
            {
                out << "episode_id,match_id,bot_guid,time_ms,action,reward,short_reward,terminal,explored,heuristic,final_score,"
                       "in_bg,in_arena";
            }
            for (size_t i = 0; i < CF_FEATURE_COUNT; ++i)
                out << ",f" << i;
            for (size_t i = 0; i < AF_COUNT; ++i)
                out << ",a" << i;
            out << "\n";
        }
        wroteHeader = true;
    }

    out << d.episodeId << "," << d.matchId << "," << d.botGuid.GetCounter() << "," << d.logTimeMs << "," << action << ","
        << reward << "," << d.shortReward << "," << terminal << "," << (d.explored ? 1 : 0) << "," << d.heuristicScore
        << "," << d.finalScore << "," << (d.inBg ? 1 : 0) << "," << (d.inArena ? 1 : 0);
    if (duelFile)
        out << "," << (d.inDuel ? 1 : 0);

    for (size_t i = 0; i < CF_FEATURE_COUNT; ++i)
        out << "," << d.features[i];
    for (size_t i = 0; i < AF_COUNT; ++i)
        out << "," << flags[i];
    out << "\n";
}

void MlDecisionLogger::Update(PlayerbotAI* botAI)
{
    if (!sPlayerbotAIConfig.mlLoggingEnabled || !botAI)
        return;

    Player* bot = botAI->GetBot();
    if (!bot)
        return;

    uint32 now = getMSTime();
    std::vector<MlPendingDecision> due;

    {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto it = pending.begin(); it != pending.end();)
        {
            if (it->botGuid != bot->GetGUID())
            {
                ++it;
                continue;
            }
            if (now < it->resolveAtMs)
            {
                ++it;
                continue;
            }
            due.push_back(*it);
            it = pending.erase(it);
        }
    }

    for (MlPendingDecision& d : due)
    {
        d.shortReward = ComputeReward(botAI, d);
        d.shortResolved = true;

        if (d.matchId == 0 && d.inDuel)
        {
            // RegisterDuelMatch may have raced past log time — re-resolve, else hold for OnDuelEnd.
            std::lock_guard<std::mutex> lock(mtx);
            auto it = duelMatchByGuid.find(d.botGuid.GetCounter());
            if (it != duelMatchByGuid.end())
                d.matchId = it->second;
            if (d.matchId == 0)
            {
                d.resolveAtMs = std::numeric_limits<uint32>::max();
                pending.push_back(d);
                continue;
            }
            matchBuffer[d.matchId].push_back(d);
            continue;
        }

        // Open-world / no match: write immediately (terminal=0).
        if (d.matchId == 0)
        {
            WriteRow(d, d.shortReward, 0.0f);
            continue;
        }

        // PvP match: buffer until OnMatchEnd / OnDuelEnd applies terminal backup.
        std::lock_guard<std::mutex> lock(mtx);
        matchBuffer[d.matchId].push_back(d);
    }
}

void MlDecisionLogger::FlushMatchDecisions(uint32 matchId, ObjectGuid botGuid, float terminal)
{
    std::vector<MlPendingDecision> rows;

    {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = matchBuffer.find(matchId);
        if (it != matchBuffer.end())
        {
            std::vector<MlPendingDecision> keep;
            keep.reserve(it->second.size());
            for (MlPendingDecision& d : it->second)
            {
                if (d.botGuid == botGuid)
                    rows.push_back(d);
                else
                    keep.push_back(d);
            }
            if (keep.empty())
                matchBuffer.erase(it);
            else
                it->second.swap(keep);
        }

        // Also resolve any still-pending decisions for this bot in this match (end before +2s delay).
        for (auto pit = pending.begin(); pit != pending.end();)
        {
            if (pit->botGuid == botGuid && pit->matchId == matchId)
            {
                rows.push_back(*pit);
                pit = pending.erase(pit);
            }
            else
                ++pit;
        }
    }

    bool anyDuel = false;
    for (MlPendingDecision const& d : rows)
        if (d.inDuel)
            anyDuel = true;

    // DEC-016: duel win/loss must dominate short shaping.
    float lambda = anyDuel ? sPlayerbotAIConfig.mlDuelTerminalLambda : sPlayerbotAIConfig.mlTerminalLambda;

    for (MlPendingDecision& d : rows)
    {
        if (!d.shortResolved)
        {
            // Best-effort: no AI context here; keep prior shortReward (0) or skip compute.
            // Prefer writing with terminal only if short never resolved.
            d.shortReward = 0.0f;
            d.shortResolved = true;
        }
        float reward = d.shortReward + lambda * terminal;
        WriteRow(d, reward, terminal);
    }
}

void MlDecisionLogger::OnMatchEnd(Battleground* bg, TeamId winnerTeam)
{
    if (!sPlayerbotAIConfig.mlLoggingEnabled || !bg)
        return;

    uint32 matchId = bg->GetInstanceID();
    for (auto const& kv : bg->GetPlayers())
    {
        Player* player = kv.second;
        if (!player)
            continue;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
        if (!botAI || botAI->IsRealPlayer())
            continue;

        float terminal = 0.0f;
        if (winnerTeam == TEAM_ALLIANCE || winnerTeam == TEAM_HORDE)
            terminal = (player->GetTeamId() == winnerTeam) ? 1.0f : -1.0f;

        // Resolve any still-pending (+2s not elapsed) with a real short reward before backup.
        std::vector<MlPendingDecision> early;
        {
            std::lock_guard<std::mutex> lock(mtx);
            for (auto pit = pending.begin(); pit != pending.end();)
            {
                if (pit->botGuid == player->GetGUID() && pit->matchId == matchId)
                {
                    early.push_back(*pit);
                    pit = pending.erase(pit);
                }
                else
                    ++pit;
            }
        }
        for (MlPendingDecision& d : early)
        {
            d.shortReward = ComputeReward(botAI, d);
            d.shortResolved = true;
            std::lock_guard<std::mutex> lock(mtx);
            matchBuffer[matchId].push_back(d);
        }

        FlushMatchDecisions(matchId, player->GetGUID(), terminal);
    }

    // Drop any orphaned buffer rows for this instance.
    std::lock_guard<std::mutex> lock(mtx);
    matchBuffer.erase(matchId);
}

void MlDecisionLogger::FlushEpisode(Player* bot, float terminal)
{
    if (!sPlayerbotAIConfig.mlLoggingEnabled || !bot)
        return;

    // `terminal` must be ∈ {+1,−1,0} — never λ*outcome (that belongs in `reward` only).
    std::vector<MlPendingDecision> mine;
    {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto it = pending.begin(); it != pending.end();)
        {
            if (it->botGuid != bot->GetGUID())
            {
                ++it;
                continue;
            }
            mine.push_back(*it);
            it = pending.erase(it);
        }
    }

    bool anyDuel = false;
    for (MlPendingDecision const& d : mine)
        if (d.inDuel)
            anyDuel = true;
    float lambda = anyDuel ? sPlayerbotAIConfig.mlDuelTerminalLambda : sPlayerbotAIConfig.mlTerminalLambda;

    for (MlPendingDecision& d : mine)
    {
        if (!d.shortResolved)
        {
            d.shortReward = 0.0f;
            d.shortResolved = true;
        }
        WriteRow(d, d.shortReward + lambda * terminal, terminal);
    }
}

void MlDecisionLogger::RegisterDuelMatch(ObjectGuid a, ObjectGuid b, uint32 matchId)
{
    std::lock_guard<std::mutex> lock(mtx);
    duelMatchByGuid[a.GetCounter()] = matchId;
    duelMatchByGuid[b.GetCounter()] = matchId;
}

void MlDecisionLogger::OnDuelEnd(Player* bot, float terminal)
{
    if (!sPlayerbotAIConfig.mlLoggingEnabled || !bot)
        return;

    uint32 matchId = 0;
    {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = duelMatchByGuid.find(bot->GetGUID().GetCounter());
        if (it != duelMatchByGuid.end())
        {
            matchId = it->second;
            duelMatchByGuid.erase(it);
        }

        // Promote duel rows that logged before RegisterDuelMatch (matchId was 0).
        if (matchId)
        {
            for (MlPendingDecision& d : pending)
            {
                if (d.botGuid == bot->GetGUID() && d.inDuel && d.matchId == 0)
                    d.matchId = matchId;
            }
            auto orphanIt = matchBuffer.find(0);
            if (orphanIt != matchBuffer.end())
            {
                std::vector<MlPendingDecision> keep;
                for (MlPendingDecision& d : orphanIt->second)
                {
                    if (d.botGuid == bot->GetGUID() && d.inDuel)
                        matchBuffer[matchId].push_back(d);
                    else
                        keep.push_back(d);
                }
                if (keep.empty())
                    matchBuffer.erase(orphanIt);
                else
                    orphanIt->second.swap(keep);
            }
        }
    }

    if (matchId)
        FlushMatchDecisions(matchId, bot->GetGUID(), terminal);

    // Any leftover pending for this bot (never got a match id) still gets ±1 terminal.
    FlushEpisode(bot, terminal);
}
