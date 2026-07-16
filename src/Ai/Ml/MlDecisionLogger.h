/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
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
#include "SharedDefines.h"

class Battleground;
class Player;
class PlayerbotAI;

struct MlPendingDecision
{
    uint64 episodeId = 0;
    uint32 matchId = 0;  // Battleground/Arena instance id; 0 = open world
    ObjectGuid botGuid;
    uint32 logTimeMs = 0;
    uint32 resolveAtMs = 0;
    CombatFeatureVector features{};
    std::string actionName;
    float heuristicScore = 1.0f;
    float finalScore = 1.0f;
    float shortReward = 0.0f;
    bool shortResolved = false;
    uint8 selfHpAtLog = 100;
    uint8 targetHpAtLog = 100;
    bool targetWasCasting = false;
    bool targetWasHealing = false;
    bool wasInterruptAction = false;
    bool inArena = false;
    bool inBg = false;
    bool inDuel = false;
    bool explored = false;
};

class MlDecisionLogger
{
public:
    static MlDecisionLogger& instance();

    void OnActionExecuted(PlayerbotAI* botAI, std::string const& actionName, float heuristicScore, float finalScore,
                          bool explored = false);
    void Update(PlayerbotAI* botAI);  // resolve delayed short rewards for this bot
    // Arena/BG end: backup terminal win/loss onto buffered match decisions.
    void OnMatchEnd(Battleground* bg, TeamId winnerTeam);
    // Apply terminal ∈ {+1,−1,0} to pending rows for this bot (reward = short + λ*terminal).
    void FlushEpisode(Player* bot, float terminal);

    void RegisterDuelMatch(ObjectGuid a, ObjectGuid b, uint32 matchId);
    void OnDuelEnd(Player* bot, float terminal);

private:
    MlDecisionLogger() = default;

    void WriteRow(MlPendingDecision const& d, float reward, float terminal);
    float ComputeReward(PlayerbotAI* botAI, MlPendingDecision const& d) const;
    void FlushMatchDecisions(uint32 matchId, ObjectGuid botGuid, float terminal);

    std::mutex mtx;
    std::deque<MlPendingDecision> pending;
    // Short-resolved PvP decisions waiting for match outcome (matchId -> rows).
    std::unordered_map<uint32, std::vector<MlPendingDecision>> matchBuffer;
    std::unordered_map<uint32, uint32> duelMatchByGuid;  // guid counter -> synthetic duel match id
    uint64 nextEpisodeId = 1;
    bool headerWritten = false;
    bool duelHeaderWritten = false;
};

#define sMlDecisionLogger MlDecisionLogger::instance()

#endif
