/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "MlDecisionLogger.h"

#include <fstream>
#include <sstream>

#include "CombatDecisionFeatures.h"
#include "HeuristicScores.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "Timer.h"

MlDecisionLogger& MlDecisionLogger::instance()
{
    static MlDecisionLogger inst;
    return inst;
}

void MlDecisionLogger::OnActionExecuted(PlayerbotAI* botAI, std::string const& actionName, float heuristicScore,
                                        float finalScore)
{
    if (!sPlayerbotAIConfig.mlLoggingEnabled || !botAI)
        return;

    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsInWorld())
        return;

    // Prefer mastered / BG / arena samples; optionally log all.
    bool interesting = botAI->HasRealPlayerMaster() || bot->InBattleground() || bot->InArena();
    if (!interesting && !sPlayerbotAIConfig.mlLogAllBots)
        return;

    AiObjectContext* context = botAI->GetAiObjectContext();
    if (!context)
        return;

    CombatFeatureVector features = AI_VALUE(CombatFeatureVector, "combat decision features");

    MlPendingDecision d;
    d.episodeId = nextEpisodeId++;
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
    d.wasInterruptAction = CombatDecisionUtil::IsInterruptAction(actionName);
    d.inArena = bot->InArena();
    d.inBg = bot->InBattleground();

    std::lock_guard<std::mutex> lock(mtx);
    // Bound memory
    while (pending.size() > 5000)
        pending.pop_front();
    pending.push_back(d);
}

float MlDecisionLogger::ComputeReward(PlayerbotAI* botAI, MlPendingDecision const& d) const
{
    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsAlive())
        return -2.0f;

    AiObjectContext* context = botAI->GetAiObjectContext();
    if (!context)
        return 0.0f;

    float reward = 0.0f;
    uint8 selfHpNow = AI_VALUE2(uint8, "health", "self target");
    Unit* target = AI_VALUE(Unit*, "current target");
    uint8 targetHpNow = target && target->IsAlive() ? static_cast<uint8>(target->GetHealthPct()) : 0;

    // Survived the window
    if (selfHpNow > 0)
        reward += 0.2f;
    if (selfHpNow + 15 < d.selfHpAtLog)
        reward -= 0.8f;  // took a big hit after the action
    if (selfHpNow > d.selfHpAtLog + 10)
        reward += 0.5f;  // recovered

    // Pressure on target
    if (target && target->IsAlive() && targetHpNow + 10 < d.targetHpAtLog)
        reward += 0.6f;
    if (target && !target->IsAlive())
        reward += 2.0f;

    // Interrupt success proxy: was casting, we used interrupt, and they are no longer casting
    if (d.wasInterruptAction && d.targetWasCasting)
    {
        bool stillCasting = target && target->IsNonMeleeSpellCast(false);
        reward += stillCasting ? -1.0f : 1.5f;
    }

    // Enemy healer pressure
    CombatFeatureVector now = AI_VALUE(CombatFeatureVector, "combat decision features");
    if (d.features[CF_HAS_ENEMY_HEALER] > 0.5f && now[CF_HAS_ENEMY_HEALER] < 0.5f)
        reward += 0.7f;

    if (d.inArena || d.inBg)
        reward *= 1.25f;

    return reward;
}

void MlDecisionLogger::WriteRow(MlPendingDecision const& d, float reward)
{
    std::string const& path = sPlayerbotAIConfig.mlLogFile;
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

    if (!headerWritten)
    {
        out << "episode_id,bot_guid,time_ms,action,reward,heuristic,final_score,"
               "in_bg,in_arena";
        for (size_t i = 0; i < CF_FEATURE_COUNT; ++i)
            out << ",f" << i;
        for (size_t i = 0; i < AF_COUNT; ++i)
            out << ",a" << i;
        out << "\n";
        headerWritten = true;
    }

    out << d.episodeId << "," << d.botGuid.GetCounter() << "," << d.logTimeMs << "," << action << "," << reward
        << "," << d.heuristicScore << "," << d.finalScore << "," << (d.inBg ? 1 : 0) << "," << (d.inArena ? 1 : 0);

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
        float reward = ComputeReward(botAI, d);
        WriteRow(d, reward);
    }
}

void MlDecisionLogger::FlushEpisode(Player* bot, float terminalReward)
{
    if (!sPlayerbotAIConfig.mlLoggingEnabled || !bot)
        return;

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

    for (MlPendingDecision& d : mine)
        WriteRow(d, terminalReward);
}
