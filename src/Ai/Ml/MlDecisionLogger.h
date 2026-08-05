/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option) any later version.
 */

#ifndef PLAYERBOTS_MLDECISIONLOGGER_H
#define PLAYERBOTS_MLDECISIONLOGGER_H

#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "CombatDecisionFeatures.h"
#include "ObjectGuid.h"

class Player;
class PlayerbotAI;

struct MlPendingDecision
{
    uint64 episodeId = 0;
    uint32 matchId = 0;
    ObjectGuid botGuid;
    uint32 logTimeMs = 0;
    uint32 resolveAtMs = 0;
    CombatFeatureVector features{};
    std::string actionName;
    // Softmax-stock τ=0 pick among legal queue candidates (DEC-025 DAgger label).
    std::string expertActionName;
    float heuristicScore = 0.0f;
    float finalScore = 0.0f;
    float shortReward = 0.0f;
    bool shortResolved = false;
    uint8 targetHpAtLog = 100;
    bool targetWasCasting = false;
    bool wasInterruptAction = false;
    // duel_v5 log-only movement columns (DEC-036).
    float realizedHeading = 0.0f;
    uint8 movementIntent = 0;
    uint8 expertMovementIntent = 0;
};

class MlDecisionLogger
{
public:
    static MlDecisionLogger& instance();

    void OnActionExecuted(PlayerbotAI* botAI, std::string const& actionName, float heuristicScore, float finalScore,
                          std::string const& expertAction = "");
    void Update(PlayerbotAI* botAI);
    void FlushEpisode(Player* bot, float terminal);

    void RegisterDuelMatch(ObjectGuid a, ObjectGuid b, uint32 matchId);
    // DEC-023: write a duel_start row with DuelCD (and full feature vector) at match begin.
    void LogDuelStartSnapshot(Player* bot, uint32 matchId);
    void OnDuelEnd(Player* bot, float terminal);

private:
    MlDecisionLogger() = default;

    void WriteRow(MlPendingDecision const& d, float reward, float terminal);
    float ComputeReward(PlayerbotAI* botAI, MlPendingDecision const& d) const;
    void FlushMatchDecisions(uint32 matchId, ObjectGuid botGuid, float terminal);

    std::mutex mtx;
    std::deque<MlPendingDecision> pending;
    std::unordered_map<uint32, std::vector<MlPendingDecision>> matchBuffer;
    std::unordered_map<uint32, uint32> duelMatchByGuid;
    uint64 nextEpisodeId = 1;
    bool headerWritten = false;
    // true ⇒ rows include expert_action (duel_v4 / DEC-025). false ⇒ legacy v3 layout.
    bool logExpertAction = true;
    // true ⇒ duel_v5 (DEC-036): movement columns + the full 90-feature vector. When appending
    // to a legacy v3/v4 file, both stay off and rows keep the old 70-feature width.
    bool logMovementCols = true;
    size_t featureColsToWrite = CF_FEATURE_COUNT;
};

#define sMlDecisionLogger MlDecisionLogger::instance()

#endif
