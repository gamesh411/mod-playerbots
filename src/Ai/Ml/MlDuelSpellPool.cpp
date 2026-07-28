/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "MlDuelSpellPool.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "Pet.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

namespace
{
std::string ToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool IsNoiseSpell(SpellInfo const* info)
{
    if (!info)
        return true;

    std::string const name = info->SpellName[0] ? info->SpellName[0] : "";
    std::string const n = ToLower(name);

    static char const* kNoise[] = {
        "opening", "closing", "stuck", "remove insignia", "grovel", "duel", "honorless target",
        "riding", "apprentice riding", "journeyman riding", "expert riding", "artisan riding",
        "cold weather flying", "flight form", "swift flight form", "travel form", "aquatic form",
        "teleport", "portal:", "hearthstone", "astral recall", "ritual of summoning",
        "create firestone", "create healthstone", "create soulstone", "create spellstone",
        "conjure food", "conjure water", "conjure mana gem", "find minerals", "find herbs",
        "find treasure", "track ", "sense undead", "sense demons"};
    for (char const* noise : kNoise)
        if (n.find(noise) != std::string::npos)
            return true;

    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        switch (info->Effects[i].Effect)
        {
            case SPELL_EFFECT_LEARN_SPELL:
            case SPELL_EFFECT_SKILL_STEP:
            case SPELL_EFFECT_TRADE_SKILL:
            case SPELL_EFFECT_ENCHANT_ITEM:
            case SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY:
            case SPELL_EFFECT_ENCHANT_HELD_ITEM:
            case SPELL_EFFECT_CREATE_ITEM:
            case SPELL_EFFECT_CREATE_MANA_GEM:
            case SPELL_EFFECT_SUMMON_PET:
            case SPELL_EFFECT_TAMECREATURE:
            case SPELL_EFFECT_TRANS_DOOR:
            case SPELL_EFFECT_SUMMON_OBJECT_WILD:
            case SPELL_EFFECT_SUMMON_OBJECT_SLOT1:
            case SPELL_EFFECT_SUMMON_OBJECT_SLOT2:
            case SPELL_EFFECT_SUMMON_OBJECT_SLOT3:
            case SPELL_EFFECT_SUMMON_OBJECT_SLOT4:
                return true;
            default:
                break;
        }
    }

    if (info->Effects[EFFECT_0].Effect == SPELL_EFFECT_CREATE_ITEM && info->ReagentCount[EFFECT_0] > 0)
        return true;

    return false;
}

void TryAddCandidate(std::vector<MlDuelSpellCandidate>& out, PlayerbotAI* botAI, uint32 spellId, Unit* duelOpponent,
                     bool petSpell)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info || info->IsPassive() || IsNoiseSpell(info))
        return;

    Unit* castTarget = nullptr;
    if (petSpell)
    {
        if (botAI->CanCastPetSpell(spellId, duelOpponent))
            castTarget = duelOpponent;
        else if (botAI->CanCastPetSpell(spellId, botAI->GetBot()))
            castTarget = botAI->GetBot();
        else
            return;
    }
    else
    {
        if (botAI->CanCastSpell(spellId, duelOpponent, true))
            castTarget = duelOpponent;
        else if (botAI->CanCastSpell(spellId, botAI->GetBot(), true))
            castTarget = botAI->GetBot();
        else
            return;
    }

    MlDuelSpellCandidate cand;
    cand.spellId = spellId;
    cand.actionName = std::to_string(spellId);
    cand.castTarget = castTarget;
    cand.petSpell = petSpell;
    out.push_back(std::move(cand));
}
}  // namespace

std::vector<MlDuelSpellCandidate> MlDuelSpellPool::Collect(PlayerbotAI* botAI, Unit* duelOpponent)
{
    std::vector<MlDuelSpellCandidate> out;
    if (!botAI || !duelOpponent)
        return out;

    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsAlive())
        return out;

    for (PlayerSpellMap::const_iterator itr = bot->GetSpellMap().begin(); itr != bot->GetSpellMap().end(); ++itr)
    {
        if (itr->second->State == PLAYERSPELL_REMOVED || !itr->second->Active)
            continue;
        if (!(itr->second->specMask & bot->GetActiveSpecMask()))
            continue;

        TryAddCandidate(out, botAI, itr->first, duelOpponent, false);
    }

    if (Pet* pet = bot->GetPet())
    {
        if (pet->IsAlive())
        {
            for (PetSpellMap::const_iterator itr = pet->m_spells.begin(); itr != pet->m_spells.end(); ++itr)
            {
                if (itr->second.state == PETSPELL_REMOVED)
                    continue;
                TryAddCandidate(out, botAI, itr->first, duelOpponent, true);
            }
        }
    }

    return out;
}

bool MlDuelSpellPool::Execute(PlayerbotAI* botAI, MlDuelSpellCandidate const& pick)
{
    if (!botAI || !pick.spellId || !pick.castTarget)
        return false;

    if (pick.petSpell)
        return botAI->CommandPetCastSpell(pick.spellId, pick.castTarget);

    return botAI->CastSpell(pick.spellId, pick.castTarget);
}
