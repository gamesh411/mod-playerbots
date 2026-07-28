/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 * Full spellbook legal pool for duel ML (DEC-014 / DEC-026).
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
    // Spell-id string for S2 vocab / logs (DEC-026: every rank is its own action).
    std::string actionName;
    Unit* castTarget = nullptr;
    bool petSpell = false;
};

namespace MlDuelSpellPool
{
// Known active non-passive combat spells currently castable (player + pet).
// All ranks kept as distinct ids (no highest-rank collapse — DEC-026).
std::vector<MlDuelSpellCandidate> Collect(PlayerbotAI* botAI, Unit* duelOpponent);

bool Execute(PlayerbotAI* botAI, MlDuelSpellCandidate const& pick);
}  // namespace MlDuelSpellPool

#endif
