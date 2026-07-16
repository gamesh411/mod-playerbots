/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "MlDuelSpellPool.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

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

    // Same spirit as ListSpellsAction ignore list + travel/profession noise.
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

    // Profession crafts: create-item + reagents.
    if (info->Effects[EFFECT_0].Effect == SPELL_EFFECT_CREATE_ITEM && info->ReagentCount[EFFECT_0] > 0)
        return true;

    return false;
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

    // Keep highest rank per lowercase name.
    std::unordered_map<std::string, MlDuelSpellCandidate> best;

    for (PlayerSpellMap::const_iterator itr = bot->GetSpellMap().begin(); itr != bot->GetSpellMap().end(); ++itr)
    {
        if (itr->second->State == PLAYERSPELL_REMOVED || !itr->second->Active)
            continue;
        if (!(itr->second->specMask & bot->GetActiveSpecMask()))
            continue;

        uint32 spellId = itr->first;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info || info->IsPassive() || IsNoiseSpell(info))
            continue;

        std::string actionName = ToLower(info->SpellName[0] ? info->SpellName[0] : "");
        if (actionName.empty())
            continue;

        Unit* castTarget = nullptr;
        if (botAI->CanCastSpell(spellId, duelOpponent, true))
            castTarget = duelOpponent;
        else if (botAI->CanCastSpell(spellId, bot, true))
            castTarget = bot;
        else
            continue;

        MlDuelSpellCandidate cand;
        cand.spellId = spellId;
        cand.actionName = std::move(actionName);
        cand.castTarget = castTarget;

        auto it = best.find(cand.actionName);
        if (it == best.end() || spellId > it->second.spellId)
            best[cand.actionName] = cand;
    }

    out.reserve(best.size());
    for (auto& kv : best)
        out.push_back(std::move(kv.second));

    return out;
}

bool MlDuelSpellPool::Execute(PlayerbotAI* botAI, MlDuelSpellCandidate const& pick)
{
    if (!botAI || !pick.spellId || !pick.castTarget)
        return false;
    return botAI->CastSpell(pick.spellId, pick.castTarget);
}
