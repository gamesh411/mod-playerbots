/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#ifndef PLAYERBOTS_MLDUELBRACKETACTION_H
#define PLAYERBOTS_MLDUELBRACKETACTION_H

#include "Action.h"

class PlayerbotAI;

class MlDuelBracketAction : public Action
{
public:
    MlDuelBracketAction(PlayerbotAI* botAI, std::string const name = "ml duel bracket") : Action(botAI, name) {}

    bool isUseful() override;
    bool Execute(Event event) override;
};

#endif
