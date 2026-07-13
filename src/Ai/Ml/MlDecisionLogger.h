/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef PLAYERBOTS_MLDECISIONLOGGER_H
#define PLAYERBOTS_MLDECISIONLOGGER_H

#include <deque>
#include <mutex>
#include <string>

#include "CombatDecisionFeatures.h"
#include "ObjectGuid.h"

class Player;
class PlayerbotAI;

struct MlPendingDecision
{
    uint64 episodeId = 0;
    ObjectGuid botGuid;
    uint32 logTimeMs = 0;
    uint32 resolveAtMs = 0;
    CombatFeatureVector features{};
    std::string actionName;
    float heuristicScore = 1.0f;
    float finalScore = 1.0f;
    uint8 selfHpAtLog = 100;
    uint8 targetHpAtLog = 100;
    bool targetWasCasting = false;
    bool targetWasHealing = false;
    bool wasInterruptAction = false;
    bool inArena = false;
    bool inBg = false;
    bool resolved = false;
};

class MlDecisionLogger
{
public:
    static MlDecisionLogger& instance();

    void OnActionExecuted(PlayerbotAI* botAI, std::string const& actionName, float heuristicScore, float finalScore);
    void Update(PlayerbotAI* botAI);  // resolve delayed rewards for this bot
    void FlushEpisode(Player* bot, float terminalReward);  // arena/bg end optional

private:
    MlDecisionLogger() = default;

    void WriteRow(MlPendingDecision const& d, float reward);
    float ComputeReward(PlayerbotAI* botAI, MlPendingDecision const& d) const;

    std::mutex mtx;
    std::deque<MlPendingDecision> pending;
    uint64 nextEpisodeId = 1;
    bool headerWritten = false;
};

#define sMlDecisionLogger MlDecisionLogger::instance()

#endif
