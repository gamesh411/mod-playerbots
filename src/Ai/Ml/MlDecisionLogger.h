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

// DEC-039 movement-CSV row (ml_movement_duel_v1): buffered per match, terminal backfilled at
// duel end; reward = Phi(now) - Phi(previous logged row) + TerminalLambda * terminal.
struct MlMovementPendingRow
{
    uint32 matchId = 0;
    ObjectGuid botGuid;
    uint8 botClass = 0;
    uint32 logTimeMs = 0;
    uint8 intent = 0;
    uint8 expertIntent = 0;
    float realizedHeading = 0.0f;
    float phi = 0.0f;
    CombatFeatureVector features{};
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

    // DEC-039: movement-channel row at the 500ms intent horizon (+ intent changes), from the
    // executor subtick. Role-derived potential is captured here; rewards resolve at duel end.
    void LogMovementRow(PlayerbotAI* botAI, uint8 intent, uint8 expertIntent, float realizedHeading);

private:
    MlDecisionLogger() = default;

    void WriteRow(MlPendingDecision const& d, float reward, float terminal);
    float ComputeReward(PlayerbotAI* botAI, MlPendingDecision const& d) const;
    void FlushMatchDecisions(uint32 matchId, ObjectGuid botGuid, float terminal);
    void WriteMovementRow(MlMovementPendingRow const& r, float reward, float terminal);
    void FlushMovementRows(uint32 matchId, ObjectGuid botGuid, float terminal);

    std::mutex mtx;
    std::deque<MlPendingDecision> pending;
    std::unordered_map<uint32, std::vector<MlPendingDecision>> matchBuffer;
    std::unordered_map<uint32, std::vector<MlMovementPendingRow>> movementBuffer;
    std::unordered_map<uint32, uint32> duelMatchByGuid;
    uint64 nextEpisodeId = 1;
    bool headerWritten = false;
    bool movementHeaderWritten = false;
    // true ⇒ rows include expert_action (duel_v4 / DEC-025). false ⇒ legacy v3 layout.
    bool logExpertAction = true;
    // true ⇒ duel_v5 and later (DEC-036): movement columns present. When appending to a legacy
    // v3/v4 file this stays off and rows keep that file's narrower layout.
    bool logMovementCols = true;
    // Feature width of the file being appended to. A fresh file gets the current vector; an
    // existing one keeps its own width, since widening mid-file silently misaligns every column
    // after the features for anything reading the file as one table.
    size_t featureColsToWrite = CF_FEATURE_COUNT;
    size_t movementFeatureColsToWrite = CF_FEATURE_COUNT;
};

#define sMlDecisionLogger MlDecisionLogger::instance()

#endif
