/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef PLAYERBOTS_PVPPOLICYSTRATEGY_H
#define PLAYERBOTS_PVPPOLICYSTRATEGY_H

#include "Multiplier.h"
#include "Strategy.h"

class Action;
class PlayerbotAI;

// Option C: scoped PvP micro-policy for battleground and arena bots.
// Heuristic policy for now; same feature vector as Option B for future tiny neural nets.
class PvpPolicyMultiplier : public Multiplier
{
public:
    PvpPolicyMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "pvp policy") {}

    float GetValue(Action* action) override;
};

class PvpPolicyStrategy : public Strategy
{
public:
    PvpPolicyStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    std::string const getName() override { return "pvp policy"; }
};

#endif
