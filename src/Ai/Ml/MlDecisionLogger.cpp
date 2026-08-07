/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option) any later version.
 */

#include "MlDecisionLogger.h"

#include <algorithm>
#include <fstream>
#include <limits>

#include "HeuristicScores.h"
#include "MlDuelMovement.h"
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

    // Sparring vs real player must not pollute duel_v4 (belt-and-suspenders with OnDuelStart skip).
    if (Player* foe = bot->duel->Opponent->ToPlayer())
    {
        PlayerbotAI* foeAI = GET_PLAYERBOT_AI(foe);
        if (!foeAI || foeAI->IsRealPlayer())
            return;
    }

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
    d.realizedHeading = sMlDuelMovement.GetRealizedHeading(d.botGuid);
    d.movementIntent = sMlDuelMovement.GetIntent(d.botGuid);
    d.expertMovementIntent = sMlDuelMovement.GetExpertIntent(d.botGuid);

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
            // duel_v6 (DEC-043): v5 layout with the vector grown to CF_PET + CF_FORM.
            logExpertAction = true;
            logMovementCols = true;
            featureColsToWrite = CF_FEATURE_COUNT;
            out << "episode_id,match_id,bot_guid,time_ms,action,expert_action,reward,short_reward,terminal,heuristic,"
                   "final_score,in_duel,realized_heading,movement_intent,expert_movement_intent";
            for (size_t i = 0; i < CF_FEATURE_COUNT; ++i)
                out << ",f" << i;
            for (size_t i = 0; i < AF_COUNT; ++i)
                out << ",a" << i;
            out << "\n";
        }
        else
        {
            // Append-compatible with existing v3/v4/v5/v6 files: keep the file's own layout.
            probe.clear();
            probe.seekg(0);
            std::string firstLine;
            std::getline(probe, firstLine);
            logExpertAction = firstLine.find("expert_action") != std::string::npos;
            logMovementCols = firstLine.find("movement_intent") != std::string::npos;
            size_t const commas = static_cast<size_t>(std::count(firstLine.begin(), firstLine.end(), ','));
            size_t const cols = commas + 1;
            // Headerless files: detect layout by width. v4 = 12 meta + 70 + 8 = 90 cols;
            // v5 = 15 meta + 90 + 8 = 113 cols. Do not silently downgrade mid-file.
            size_t const v4Cols = 12 + CF_LEGACY_ABILITY_FEATURE_COUNT + AF_COUNT;  // 90
            size_t const v5Cols = 15 + CF_MOVE_HEAD_FEATURE_COUNT + AF_COUNT;       // 113
            if (!logMovementCols && !firstLine.empty() && cols >= v5Cols)
                logMovementCols = true;
            if (!logExpertAction && !firstLine.empty() && cols >= v4Cols)
                logExpertAction = true;
            // Trust the file's own width rather than the current vector: appending v6-width rows
            // to a v5 farm would shift every action-flag column by 22 for every reader.
            size_t const metaCols = 11 + (logExpertAction ? 1 : 0) + (logMovementCols ? 3 : 0);
            featureColsToWrite = cols > metaCols + AF_COUNT
                                     ? std::min<size_t>(cols - metaCols - AF_COUNT, CF_FEATURE_COUNT)
                                     : (logMovementCols ? CF_MOVE_HEAD_FEATURE_COUNT
                                                        : size_t(CF_LEGACY_ABILITY_FEATURE_COUNT));
        }
        headerWritten = true;
    }

    out << d.episodeId << "," << d.matchId << "," << d.botGuid.GetCounter() << "," << d.logTimeMs << "," << action;
    if (logExpertAction)
        out << "," << expert;
    out << "," << reward << "," << d.shortReward << "," << terminal << "," << d.heuristicScore << "," << d.finalScore
        << ",1";
    if (logMovementCols)
        out << "," << d.realizedHeading << "," << uint32(d.movementIntent) << "," << uint32(d.expertMovementIntent);
    for (size_t i = 0; i < featureColsToWrite; ++i)
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
    if (!botAI || botAI->IsRealPlayer())
        return;

    if (bot->duel && bot->duel->Opponent)
    {
        if (Player* foe = bot->duel->Opponent->ToPlayer())
        {
            PlayerbotAI* foeAI = GET_PLAYERBOT_AI(foe);
            if (!foeAI || foeAI->IsRealPlayer())
                return;
        }
    }

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
    d.realizedHeading = sMlDuelMovement.GetRealizedHeading(d.botGuid);
    d.movementIntent = sMlDuelMovement.GetIntent(d.botGuid);
    d.expertMovementIntent = sMlDuelMovement.GetExpertIntent(d.botGuid);
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
    {
        FlushMatchDecisions(matchId, bot->GetGUID(), terminal);
        FlushMovementRows(matchId, bot->GetGUID(), terminal);
    }
    FlushEpisode(bot, terminal);
}

namespace
{
// DEC-039 role-derived potentials in [0,1]. Frost mirrors the DEC-036 mover bands
// (retreat < 15, stand 15-30, leash 35); Arms rewards melee contact with a linear ramp.
float MovementPotential(Player* bot, Unit* foe)
{
    float const d = bot->GetDistance(foe);
    if (bot->getClass() == CLASS_MAGE)
    {
        if (!bot->IsWithinLOSInMap(foe))
            return 0.0f;
        if (d <= 5.0f)
            return 0.0f;
        if (d < 15.0f)
            return (d - 5.0f) / 10.0f;
        if (d <= 30.0f)
            return 1.0f;
        if (d < 35.0f)
            return (35.0f - d) / 5.0f;
        return 0.0f;
    }

    if (bot->IsWithinMeleeRange(foe))
        return 1.0f;
    return std::clamp(1.0f - (d - 5.0f) / 25.0f, 0.0f, 1.0f);
}
}  // namespace

void MlDecisionLogger::LogMovementRow(PlayerbotAI* botAI, uint8 intent, uint8 expertIntent, float realizedHeading)
{
    if (!sPlayerbotAIConfig.mlLoggingEnabled || !botAI)
        return;

    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsInWorld() || !bot->duel || !bot->duel->Opponent)
        return;

    // Sparring vs a real player never feeds the farm CSVs (same guard as ability rows).
    if (Player* foe = bot->duel->Opponent->ToPlayer())
    {
        PlayerbotAI* foeAI = GET_PLAYERBOT_AI(foe);
        if (!foeAI || foeAI->IsRealPlayer())
            return;
    }

    AiObjectContext* context = botAI->GetAiObjectContext();
    if (!context)
        return;
    auto* featureValue = context->GetValue<CombatFeatureVector>("combat decision features");
    if (!featureValue)
        return;

    MlMovementPendingRow r;
    r.botGuid = bot->GetGUID();
    r.botClass = bot->getClass();
    r.logTimeMs = getMSTime();
    r.intent = intent;
    r.expertIntent = expertIntent;
    r.realizedHeading = realizedHeading;
    r.phi = MovementPotential(bot, bot->duel->Opponent);
    r.features = featureValue->Get();

    std::lock_guard<std::mutex> lock(mtx);
    auto it = duelMatchByGuid.find(r.botGuid.GetCounter());
    if (it == duelMatchByGuid.end())
        return;  // movement rows only exist inside bracket matches (terminal backfill needs one)
    r.matchId = it->second;

    std::vector<MlMovementPendingRow>& rows = movementBuffer[r.matchId];
    if (rows.size() >= 20000)
        return;  // runaway-match guard; a real duel logs ~100 rows/bot
    rows.push_back(std::move(r));
}

void MlDecisionLogger::WriteMovementRow(MlMovementPendingRow const& r, float reward, float terminal)
{
    std::string const& path = sPlayerbotAIConfig.mlDuelMovementLogFile;
    if (path.empty())
        return;

    std::lock_guard<std::mutex> fileLock(sPlayerbotAIConfig.m_logMtx);
    std::ofstream out(path.c_str(), std::ios::app);
    if (!out)
        return;

    constexpr size_t kMovementMetaCols = 9;
    if (!movementHeaderWritten)
    {
        std::ifstream probe(path.c_str(), std::ios::binary | std::ios::ate);
        if (!(probe.good() && probe.tellg() > 0))
        {
            movementFeatureColsToWrite = CF_FEATURE_COUNT;
            out << "match_id,bot_guid,bot_class,time_ms,movement_intent,expert_movement_intent,"
                   "realized_heading,reward,terminal";
            for (size_t i = 0; i < CF_FEATURE_COUNT; ++i)
                out << ",f" << i;
            out << "\n";
        }
        else
        {
            // Keep an existing movement farm's width; the M1 heads read the 90-D prefix either way.
            probe.clear();
            probe.seekg(0);
            std::string firstLine;
            std::getline(probe, firstLine);
            size_t const cols = static_cast<size_t>(std::count(firstLine.begin(), firstLine.end(), ',')) + 1;
            movementFeatureColsToWrite = cols > kMovementMetaCols
                                             ? std::min<size_t>(cols - kMovementMetaCols, CF_FEATURE_COUNT)
                                             : size_t(CF_MOVE_HEAD_FEATURE_COUNT);
        }
        movementHeaderWritten = true;
    }

    out << r.matchId << "," << r.botGuid.GetCounter() << "," << uint32(r.botClass) << "," << r.logTimeMs << ","
        << uint32(r.intent) << "," << uint32(r.expertIntent) << "," << r.realizedHeading << "," << reward << ","
        << terminal;
    for (size_t i = 0; i < movementFeatureColsToWrite; ++i)
        out << "," << r.features[i];
    out << "\n";
}

void MlDecisionLogger::FlushMovementRows(uint32 matchId, ObjectGuid botGuid, float terminal)
{
    std::vector<MlMovementPendingRow> rows;
    {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = movementBuffer.find(matchId);
        if (it == movementBuffer.end())
            return;
        std::vector<MlMovementPendingRow> keep;
        for (MlMovementPendingRow& r : it->second)
        {
            if (r.botGuid == botGuid)
                rows.push_back(std::move(r));
            else
                keep.push_back(std::move(r));
        }
        if (keep.empty())
            movementBuffer.erase(it);
        else
            it->second.swap(keep);
    }

    // Consecutive-row potential deltas keep the shaping telescope intact under subsampling
    // (DEC-039); the first row anchors at zero so the sum is Phi(end) - Phi(start).
    float prevPhi = rows.empty() ? 0.0f : rows.front().phi;
    for (MlMovementPendingRow const& r : rows)
    {
        float const reward = (r.phi - prevPhi) + sPlayerbotAIConfig.mlDuelMovementTerminalLambda * terminal;
        prevPhi = r.phi;
        WriteMovementRow(r, reward, terminal);
    }
}
