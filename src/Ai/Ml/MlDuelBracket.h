/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 * Duel bracket: configurable spec-pairs, park pads, waitlist matching while arena-idle.
 */

#ifndef PLAYERBOTS_MLDUELBRACKET_H
#define PLAYERBOTS_MLDUELBRACKET_H

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ObjectGuid.h"
#include "SharedDefines.h"

class Player;
class PlayerbotAI;

struct MlDuelSpecKey
{
    uint8 cls = 0;
    uint8 tab = 0;  // AiFactory::GetPlayerSpecTab

    bool operator==(MlDuelSpecKey const& o) const { return cls == o.cls && tab == o.tab; }
};

struct MlDuelPair
{
    MlDuelSpecKey a;
    MlDuelSpecKey b;
};

struct MlDuelPark
{
    uint32 mapId = 0;
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float o = 0.f;
};

class MlDuelBracket
{
public:
    static MlDuelBracket& instance();

    void LoadFromConfig();
    bool IsEnabled() const { return enabled; }

    // Class bitmask for create/login filters (bit = 1 << (class-1)). 0 = no filter.
    uint32 AllowedClassMask() const { return allowedClassMask; }
    bool IsClassAllowed(uint8 cls) const;
    bool IsBotEligibleSpec(Player* bot) const;
    bool TryGetComplement(MlDuelSpecKey const& key, MlDuelSpecKey& outComplement) const;

    // Waitlist: returns true if a duel was initiated this call.
    bool TryMatchOrQueue(PlayerbotAI* botAI);

    void OnDuelStart(Player* p1, Player* p2);
    void OnDuelEnd(Player* winner, Player* loser, DuelCompleteType type);
    void ClearWaiting(ObjectGuid guid);

    MlDuelPark const& AlliancePark() const { return alliancePark; }
    MlDuelPark const& HordePark() const { return hordePark; }
    std::vector<MlDuelPair> const& Pairs() const { return pairs; }

    // Forced RandomClassSpecProb overrides when bracket enabled (class -> tab -> 100).
    void ApplySpecProbOverrides();

private:
    MlDuelBracket() = default;

    bool ParsePairs(std::string const& raw);
    bool ParsePark(std::string const& raw, MlDuelPark& out);
    bool EnsureAtPark(Player* bot);
    bool AreaAllowsDuels(Player* bot) const;
    bool IsIdleEligible(Player* bot, PlayerbotAI* botAI) const;
    bool InitiateDuel(Player* challenger, Player* opponent);
    MlDuelSpecKey SpecOf(Player* bot) const;

    bool enabled = false;
    uint32 allowedClassMask = 0;
    uint32 maxMatchRange = 80;
    uint32 rematchCooldownMs = 8000;
    std::vector<MlDuelPair> pairs;
    MlDuelPark alliancePark;
    MlDuelPark hordePark;

    struct Waiter
    {
        ObjectGuid guid;
        MlDuelSpecKey key;
        uint32 queuedAtMs = 0;
    };
    mutable std::recursive_mutex mtx;
    std::vector<Waiter> waiting;
    std::unordered_map<uint32, uint32> lastDuelEndMs;  // guid counter -> time
    std::unordered_map<uint32, uint32> duelMatchIds;   // guid counter -> synthetic match id
    uint32 nextDuelMatchId = 1;
};

#define sMlDuelBracket MlDuelBracket::instance()

#endif
