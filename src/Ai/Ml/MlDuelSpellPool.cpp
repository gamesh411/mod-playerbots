/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "MlDuelSpellPool.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "CharmInfo.h"
#include "Pet.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"

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

// DEC-030: auto-attack toggles are not casts - they flip a persistent auto-repeat state, so as
// head actions they are always-legal degenerate picks that starve real casts. Melee (6603) is
// maintained by the Engine as engagement scaffolding (like scripted movement); ranged/wand
// auto-repeat spells carry SPELL_ATTR2_AUTO_REPEAT and are filtered below.
constexpr uint32 SPELL_MELEE_AUTO_ATTACK = 6603;

void TryAddCandidate(std::vector<MlDuelSpellCandidate>& out, PlayerbotAI* botAI, uint32 spellId, Unit* duelOpponent,
                     bool petSpell)
{
    if (spellId == SPELL_MELEE_AUTO_ATTACK)
        return;

    // Dual-spec activation spells (Activate Primary/Secondary Spec): always-legal persistent
    // state flips in every dual-spec character's book - toggle family, not duel casts.
    if (spellId == 63644 || spellId == 63645)
        return;

    // DEC-030 auto-repeat toggles by explicit id: 3018 (ranged Shoot) passes the
    // SPELL_ATTR2_AUTO_REPEAT filter on this core, and any id the trainer drops as a label MUST
    // be masked here too - a dropped label keeps an untrained vocab slot whose arbitrary logits
    // can win live argmax (measured: 1.5k tau=0 picks of 3018).
    switch (spellId)
    {
        case 75:    // Auto Shot
        case 2764:  // Throw
        case 3018:  // Shoot (ranged)
        case 5019:  // Shoot (wand)
            return;
        default:
            break;
    }

    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info || info->IsPassive() || info->IsAutoRepeatRangedSpell() || IsNoiseSpell(info))
        return;

    // On-next-swing spells (Heroic Strike / Cleave / Maul) queue on the swing timer the way
    // toggles queue state: re-picking while one is pending never resolves anything and starves
    // the tick (the Cleave-spam collapse). Mask them all until the queued swing lands or clears.
    if ((info->HasAttribute(SPELL_ATTR0_ON_NEXT_SWING) || info->HasAttribute(SPELL_ATTR0_ON_NEXT_SWING_NO_DAMAGE)) &&
        botAI->GetBot()->GetCurrentSpell(CURRENT_MELEE_SPELL))
        return;

    // DEC-034: shapeshift-form spells (warrior stances, druid forms) are always-legal persistent
    // state flips, and the feature vector has no form bit - the head cannot condition on them, so
    // they are an argmax sink (stance collapse). Form control stays scripted until a form feature
    // lands.
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        if (info->Effects[i].ApplyAuraName == SPELL_AURA_MOD_SHAPESHIFT)
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

    // GetGuardianPet, not GetPet: the unglyphed Water Elemental is a Guardian with a Unit-high
    // guid, which Player::GetPet refuses - via GetPet the pet-root combo (Freeze 33395) was never
    // even a candidate. Autocast-enabled pet spells (Waterbolt) stay excluded: the pet AI already
    // repeats them, so as head actions they are toggle-like no-ops (DEC-030 care).
    if (Guardian* pet = bot->GetGuardianPet())
    {
        if (pet->IsAlive())
        {
            // DEC-032 diagnostics: confirm the elemental's command spells are visible from the
            // pool. Remove once Freeze (33395) shows up in farm data.
            static time_t lastPetProbe = 0;
            time_t const now = time(nullptr);
            if (now - lastPetProbe > 60)
            {
                lastPetProbe = now;
                LOG_INFO("playerbots", "DEC-032 pet probe owner={} pet={} isPet={} hasFreeze={} canFreeze={}",
                         bot->GetName(), pet->GetName(), pet->IsPet() ? 1 : 0,
                         pet->HasSpell(33395) ? 1 : 0,
                         botAI->CanCastPetSpell(33395, duelOpponent) ? 1 : 0);
            }
            // Union of PetSpellMap and creature template spells: the temporary Water Elemental is
            // a Pet whose map never learns its command spells (Freeze 33395) - the template is
            // what the client pet bar actually casts from.
            CharmInfo* charmInfo = pet->GetCharmInfo();
            auto isCharmAutocast = [charmInfo](uint32 spellId)
            {
                if (!charmInfo)
                    return false;
                for (uint8 slot = 0; slot < MAX_SPELL_CHARM; ++slot)
                    if (CharmSpellInfo const* charmSpell = charmInfo->GetCharmSpell(slot))
                        if (charmSpell->GetAction() == spellId && charmSpell->GetType() == ACT_ENABLED)
                            return true;
                return false;
            };

            std::vector<uint32> petSpellIds;
            if (Pet* asPet = pet->ToPet())
                for (PetSpellMap::const_iterator itr = asPet->m_spells.begin(); itr != asPet->m_spells.end(); ++itr)
                {
                    if (itr->second.state == PETSPELL_REMOVED || itr->second.active == ACT_ENABLED)
                        continue;
                    petSpellIds.push_back(itr->first);
                }
            for (uint8 i = 0; i < MAX_CREATURE_SPELLS; ++i)
            {
                uint32 const spellId = pet->m_spells[i];
                if (spellId && std::find(petSpellIds.begin(), petSpellIds.end(), spellId) == petSpellIds.end())
                    petSpellIds.push_back(spellId);
            }

            for (uint32 spellId : petSpellIds)
                if (!isCharmAutocast(spellId))
                    TryAddCandidate(out, botAI, spellId, duelOpponent, true);
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
