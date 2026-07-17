/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option) any later version.
 */

#include "MlDecisionLogger.h"

#include <algorithm>
#include <fstream>
#include <limits>

#include "HeuristicScores.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "Timer.h"

MlDecisionLogger& MlDecisionLogger::instance()
{
    static MlDecisionLogger inst;
    return inst;
}

void MlDecisionLogger::OnActionExecuted(PlayerbotAI* botAI, std::string const& actionName, float heuristicScore,
                                        float finalScore, std::string const& expertAction)
{
    if (!sPlayerbotAIConfig.mlLoggingEnabled || !botAI)
        return;

    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsInWorld() || !bot->duel || !bot->duel->Opponent)
        return;

    if (actionName.empty() || CombatDecisionUtil::IsMetaAction(actionName))
        return;

    AiObjectContext* context = botAI->GetAiObjectContext();
    if (!context)
        return;

    MlPendingDecision d;
    d.botGuid = bot->GetGUID();
    d.logTimeMs = getMSTime();
    d.resolveAtMs = d.logTimeMs + sPlayerbotAIConfig.mlRewardDelayMs;
    d.features = AI_VALUE(CombatFeatureVector, "combat decision features");
    d.actionName = actionName;
    d.expertActionName = expertAction.empty() ? actionName : expertAction;
    d.heuristicScore = heuristicScore;
    d.finalScore = finalScore;
    d.targetHpAtLog = static_cast<uint8>(d.features[CF_TARGET_HEALTH] * 100.0f);
    d.targetWasCasting = d.features[CF_TARGET_IS_CASTING] > 0.5f;
    d.wasInterruptAction = CombatDecisionUtil::IsInterruptAction(actionName);

    std::lock_guard<std::mutex> lock(mtx);
    d.episodeId = nextEpisodeId++;
    auto it = duelMatchByGuid.find(d.botGuid.GetCounter());
    if (it != duelMatchByGuid.end())
        d.matchId = it->second;

    while (pending.size() > 5000)
        pending.pop_front();
    pending.push_back(std::move(d));
}

float MlDecisionLogger::ComputeReward(PlayerbotAI* botAI, MlPendingDecision const& d) const
{
    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsAlive())
        return 0.0f;

    Unit* foe = bot->duel && bot->duel->Opponent ? bot->duel->Opponent : nullptr;
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

    if (reward > 2.0f)
        reward = 2.0f;
    if (reward < -2.0f)
        reward = -2.0f;
    return reward;
}

void MlDecisionLogger::WriteRow(MlPendingDecision const& d, float reward, float terminal)
{
    std::string const& path = sPlayerbotAIConfig.mlDuelBracketLogFile;
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

    std::string expert = d.expertActionName.empty() ? d.actionName : d.expertActionName;
    for (char& c : expert)
        if (c == ',')
            c = ';';

    if (!headerWritten)
    {
        std::ifstream probe(path.c_str(), std::ios::binary | std::ios::ate);
        bool const fileHasContent = probe.good() && probe.tellg() > 0;
        if (!fileHasContent)
        {
            // duel_v4: expert_action for DAgger (DEC-025).
            logExpertAction = true;
            out << "episode_id,match_id,bot_guid,time_ms,action,expert_action,reward,short_reward,terminal,heuristic,"
                   "final_score,in_duel";
            for (size_t i = 0; i < CF_FEATURE_COUNT; ++i)
                out << ",f" << i;
            for (size_t i = 0; i < AF_COUNT; ++i)
                out << ",a" << i;
            out << "\n";
        }
        else
        {
            // Append-compatible with existing v3 (headerless or without expert_action).
            probe.clear();
            probe.seekg(0);
            std::string firstLine;
            std::getline(probe, firstLine);
            logExpertAction = firstLine.find("expert_action") != std::string::npos;
            // Headerless duel_v4 is 90 cols (12 meta incl. expert_action + 70 + 8). If a prior
            // process wrote headerless v4 then restarted, the first data line has no substring
            // "expert_action" — detect by width so we do not silently downgrade to 89-col v3
            // mid-file (shifts terminal and breaks eval).
            if (!logExpertAction && !firstLine.empty())
            {
                size_t const commas = static_cast<size_t>(std::count(firstLine.begin(), firstLine.end(), ','));
                size_t const cols = commas + 1;
                size_t const v4Cols = 12 + CF_FEATURE_COUNT + AF_COUNT; // 90
                if (cols >= v4Cols)
                    logExpertAction = true;
            }
        }
        headerWritten = true;
    }

    out << d.episodeId << "," << d.matchId << "," << d.botGuid.GetCounter() << "," << d.logTimeMs << "," << action;
    if (logExpertAction)
        out << "," << expert;
    out << "," << reward << "," << d.shortReward << "," << terminal << "," << d.heuristicScore << "," << d.finalScore
        << ",1";
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
            if (it->botGuid != bot->GetGUID() || now < it->resolveAtMs)
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
        std::lock_guard<std::mutex> lock(mtx);
        if (d.matchId == 0)
        {
            auto it = duelMatchByGuid.find(d.botGuid.GetCounter());
            if (it != duelMatchByGuid.end())
                d.matchId = it->second;
        }
        if (d.matchId == 0)
        {
            d.resolveAtMs = std::numeric_limits<uint32>::max();
            pending.push_back(std::move(d));
            continue;
        }
        matchBuffer[d.matchId].push_back(std::move(d));
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
            for (MlPendingDecision& d : it->second)
            {
                if (d.botGuid == botGuid)
                    rows.push_back(std::move(d));
                else
                    keep.push_back(std::move(d));
            }
            if (keep.empty())
                matchBuffer.erase(it);
            else
                it->second.swap(keep);
        }

        for (auto it = pending.begin(); it != pending.end();)
        {
            if (it->botGuid == botGuid && it->matchId == matchId)
            {
                rows.push_back(*it);
                it = pending.erase(it);
            }
            else
                ++it;
        }
    }

    for (MlPendingDecision& d : rows)
    {
        if (!d.shortResolved)
            d.shortResolved = true;
        WriteRow(d, d.shortReward + sPlayerbotAIConfig.mlDuelTerminalLambda * terminal, terminal);
    }
}

void MlDecisionLogger::FlushEpisode(Player* bot, float terminal)
{
    if (!sPlayerbotAIConfig.mlLoggingEnabled || !bot)
        return;

    std::vector<MlPendingDecision> rows;
    {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto it = pending.begin(); it != pending.end();)
        {
            if (it->botGuid == bot->GetGUID())
            {
                rows.push_back(*it);
                it = pending.erase(it);
            }
            else
                ++it;
        }
    }

    for (MlPendingDecision& d : rows)
        WriteRow(d, d.shortReward + sPlayerbotAIConfig.mlDuelTerminalLambda * terminal, terminal);
}

void MlDecisionLogger::RegisterDuelMatch(ObjectGuid a, ObjectGuid b, uint32 matchId)
{
    std::lock_guard<std::mutex> lock(mtx);
    duelMatchByGuid[a.GetCounter()] = matchId;
    duelMatchByGuid[b.GetCounter()] = matchId;
}

void MlDecisionLogger::LogDuelStartSnapshot(Player* bot, uint32 matchId)
{
    if (!sPlayerbotAIConfig.mlLoggingEnabled || !bot || !matchId)
        return;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return;

    AiObjectContext* context = botAI->GetAiObjectContext();
    if (!context)
        return;

    auto* featureValue = context->GetValue<CombatFeatureVector>("combat decision features");
    if (!featureValue)
        return;

    MlPendingDecision d;
    d.matchId = matchId;
    d.botGuid = bot->GetGUID();
    d.logTimeMs = getMSTime();
    d.features = featureValue->Get();
    d.actionName = "duel_start";
    d.heuristicScore = 0.0f;
    d.finalScore = 0.0f;
    d.shortReward = 0.0f;
    d.shortResolved = true;
    {
        std::lock_guard<std::mutex> lock(mtx);
        d.episodeId = nextEpisodeId++;
    }
    WriteRow(d, 0.0f, 0.0f);
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
        for (MlPendingDecision& d : pending)
            if (d.botGuid == bot->GetGUID() && d.matchId == 0)
                d.matchId = matchId;
    }

    if (matchId)
        FlushMatchDecisions(matchId, bot->GetGUID(), terminal);
    FlushEpisode(bot, terminal);
}
