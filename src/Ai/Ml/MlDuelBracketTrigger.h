/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#ifndef PLAYERBOTS_MLDUELBRACKETTRIGGER_H
#define PLAYERBOTS_MLDUELBRACKETTRIGGER_H

#include "Trigger.h"

class PlayerbotAI;

class MlDuelBracketTrigger : public Trigger
{
public:
    MlDuelBracketTrigger(PlayerbotAI* botAI, std::string const name = "ml duel bracket") : Trigger(botAI, name, 2) {}

    bool IsActive() override;
};

#endif
