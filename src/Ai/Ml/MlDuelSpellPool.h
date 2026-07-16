/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 * Full spellbook legal pool for duel ML exploration (DEC-014).
 */

#ifndef PLAYERBOTS_MLDUELSPELLPOOL_H
#define PLAYERBOTS_MLDUELSPELLPOOL_H

#include <string>
#include <vector>

#include "Define.h"

class PlayerbotAI;
class Unit;

struct MlDuelSpellCandidate
{
    uint32 spellId = 0;
    std::string actionName;  // lowercase spell name for logs / ranking vocab
    Unit* castTarget = nullptr;
};

namespace MlDuelSpellPool
{
// Known, active, non-passive combat-relevant spells that CanCastSpell right now.
std::vector<MlDuelSpellCandidate> Collect(PlayerbotAI* botAI, Unit* duelOpponent);

bool Execute(PlayerbotAI* botAI, MlDuelSpellCandidate const& pick);
}  // namespace MlDuelSpellPool

#endif
