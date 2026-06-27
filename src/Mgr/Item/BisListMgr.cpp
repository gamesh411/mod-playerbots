/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BisListMgr.h"

#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Field.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QueryResult.h"
#include "ReputationMgr.h"

void BisListMgr::LoadAll()
{
    _bis.clear();

    QueryResult result = PlayerbotsDatabase.Query(
        "SELECT class, tab, slot, faction, auto_gear_score_limit, item_id FROM playerbots_bis_gear");
    if (!result)
    {
        LOG_INFO("server.loading", "playerbots_bis_gear table missing or empty");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();
        uint8  cls       = fields[0].Get<uint8>();
        uint8  tab       = fields[1].Get<uint8>();
        uint8  slot      = fields[2].Get<uint8>();
        uint8  faction   = fields[3].Get<uint8>();
        uint16 autoGearScoreLimit = fields[4].Get<uint16>();
        uint32 item      = fields[5].Get<uint32>();

        _bis[autoGearScoreLimit][MakeKey(cls, tab)][faction][slot] = item;
        ++count;
    } while (result->NextRow());

    LOG_INFO("server.loading", "Loaded {} BiS entries across {} item levels",
             count, static_cast<uint32>(_bis.size()));
}

uint16 BisListMgr::GetMinIlvl() const
{
    if (_bis.empty())
        return 0;
    return _bis.begin()->first;
}

uint16 BisListMgr::GetMaxIlvl() const
{
    if (_bis.empty())
        return 0;
    return _bis.rbegin()->first;
}

bool BisListMgr::CanBotEquipBisItem(Player* bot, uint8 slot, ItemTemplate const* proto) const
{
    if (!bot || !proto)
        return false;

    if (proto->RequiredReputationFaction && proto->RequiredReputationRank > 0)
    {
        if (FactionEntry const* fac = sFactionStore.LookupEntry(proto->RequiredReputationFaction))
        {
            ReputationRank requiredRank = static_cast<ReputationRank>(proto->RequiredReputationRank);
            if (bot->GetReputationRank(proto->RequiredReputationFaction) < requiredRank)
            {
                int32 standing = ReputationMgr::ReputationRankToStanding(
                                     static_cast<ReputationRank>(requiredRank - 1)) + 1;
                bot->GetReputationMgr().SetReputation(fac, standing);
            }
        }
    }

    if (bot->CanUseItem(proto) != EQUIP_ERR_OK)
        return false;

    uint16 dest = 0;
    return bot->CanEquipNewItem(slot, dest, proto->ItemId, false) == EQUIP_ERR_OK;
}

uint16 BisListMgr::GetMaxEquipableIlvl(Player* bot, uint8 cls, uint8 tab, uint8 faction) const
{
    if (!bot || _bis.empty())
        return 0;

    for (auto it = _bis.rbegin(); it != _bis.rend(); ++it)
    {
        std::map<uint8, uint32> const bisMap = GetBisFor(it->first, cls, tab, faction);
        if (bisMap.empty())
            continue;

        bool allEquipable = true;
        for (auto const& kv : bisMap)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(kv.second);
            if (!CanBotEquipBisItem(bot, kv.first, proto))
            {
                allEquipable = false;
                break;
            }
        }

        if (allEquipable)
            return it->first;
    }

    return 0;
}

std::map<uint8, uint32> BisListMgr::GetBisFor(uint16 autoGearScoreLimit, uint8 cls, uint8 tab, uint8 faction) const
{
    auto ilvlIt = _bis.find(autoGearScoreLimit);
    if (ilvlIt == _bis.end())
        return {};

    auto comboIt = ilvlIt->second.find(MakeKey(cls, tab));
    if (comboIt == ilvlIt->second.end())
        return {};

    std::map<uint8, uint32> result;

    // Base: faction=0 (Both).
    auto bothIt = comboIt->second.find(0);
    if (bothIt != comboIt->second.end())
        result = bothIt->second;

    // Faction-specific overrides Both.
    if (faction == 1 || faction == 2)
    {
        auto facIt = comboIt->second.find(faction);
        if (facIt != comboIt->second.end())
            for (auto const& kv : facIt->second)
                result[kv.first] = kv.second;
    }

    return result;
}

std::map<uint8, uint32> BisListMgr::GetBisForNearest(uint16 requestedIlvl, uint16 maxDrop, uint8 cls, uint8 tab,
                                                    uint8 faction, uint16* outResolved) const
{
    uint16 floor = requestedIlvl > maxDrop ? requestedIlvl - maxDrop : 1;
    for (uint16 try_ilvl = requestedIlvl; try_ilvl >= floor; --try_ilvl)
    {
        auto result = GetBisFor(try_ilvl, cls, tab, faction);
        if (!result.empty())
        {
            if (outResolved)
                *outResolved = try_ilvl;
            return result;
        }
        if (try_ilvl == 0)
            break;
    }
    if (outResolved)
        *outResolved = 0;
    return {};
}
