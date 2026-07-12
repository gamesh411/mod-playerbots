/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef PLAYERBOTS_HYBRIDRELEVANCESTRATEGY_H
#define PLAYERBOTS_HYBRIDRELEVANCESTRATEGY_H

#include "Multiplier.h"
#include "Strategy.h"

class Action;
class PlayerbotAI;

// Option B: hybrid relevance scorer for every bot.
// Starts as a budgeted heuristic ranker; interface is ready for an ONNX/export model later.
class HybridRelevanceMultiplier : public Multiplier
{
public:
    HybridRelevanceMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "hybrid relevance") {}

    float GetValue(Action* action) override;
};

class HybridRelevanceStrategy : public Strategy
{
public:
    HybridRelevanceStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
    std::string const getName() override { return "hybrid relevance"; }
};

#endif
