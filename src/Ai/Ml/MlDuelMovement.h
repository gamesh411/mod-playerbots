/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 * DEC-036 M0 movement substrate: packet-driven intent executor + scripted intent movers.
 */

#ifndef PLAYERBOTS_MLDUELMOVEMENT_H
#define PLAYERBOTS_MLDUELMOVEMENT_H

#include <mutex>
#include <string>
#include <unordered_map>

#include "ObjectGuid.h"

class Player;
class PlayerbotAI;
class Unit;
class WorldObject;

// 9-way foe-bearing-relative intent vocabulary (DEC-036). Direction of intent i (i >= 1)
// is foeBearing + (i - 1) * 45 degrees; probes share the same indexing (probe[i - 1]).
enum MlMovementIntent : uint8
{
    ML_MOVE_INTENT_HOLD = 0,
    ML_MOVE_INTENT_TOWARD,        // 0 deg: at foe
    ML_MOVE_INTENT_TOWARD_LEFT,   // 45 deg (counterclockwise, matching orientation math)
    ML_MOVE_INTENT_LEFT,          // 90
    ML_MOVE_INTENT_AWAY_LEFT,     // 135
    ML_MOVE_INTENT_AWAY,          // 180: dead away
    ML_MOVE_INTENT_AWAY_RIGHT,    // 225
    ML_MOVE_INTENT_RIGHT,         // 270
    ML_MOVE_INTENT_TOWARD_RIGHT,  // 315
    ML_MOVE_INTENT_COUNT
};

struct MlBotMovementState
{
    uint32 accumMs = 0;
    uint8 intent = ML_MOVE_INTENT_HOLD;
    uint8 expertIntent = ML_MOVE_INTENT_HOLD;
    // Heading actually executed this subtick, relative to foe bearing, radians [-pi, pi].
    float realizedHeading = 0.0f;
    float probes[8] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    uint32 probeStampMs = 0;
    bool moving = false;
    uint32 moveFlags = 0;
    float facing = 0.0f;
    int8 strafeSide = 1;  // sticky strafe-away side; flip only when probes block it
    bool suppressed = false;  // stopped for a hardcast / channel
    // Broadcast throttle: observers extrapolate from move flags, so steady motion only needs a
    // heartbeat every ~500 ms (real-client cadence); in between the position integrates silently.
    uint32 lastPacketMs = 0;
    // Intent debounce: a candidate intent must persist one extra subtick before it switches,
    // so range-boundary jitter cannot flap START/STOP packets.
    uint8 pendingIntent = 0;
    uint8 pendingCount = 0;
    // Jump-turn (atomic until landing)
    bool airborne = false;
    uint32 jumpElapsedMs = 0;
    float jumpDirWorld = 0.0f;
    float jumpSpeedXY = 0.0f;
    float jumpStartZ = 0.0f;
    bool restoreFacingOnLand = false;
    float facingAfterLand = 0.0f;
};

// Singleton executor: one 100 ms subtick per bot riding every core tick (ability loop untouched).
// Motion is direct velocity control via synthesized client movement packets dispatched
// synchronously through the bot's own WorldSession (map-thread, same as real client movement).
class MlDuelMovement
{
public:
    static MlDuelMovement& instance();

    // Called from PlayerbotAI::UpdateAI before the react-delay gate, every core tick.
    void Update(PlayerbotAI* botAI, uint32 elapsed);

    // True while the movement channel drives this bot's duel (conf enable + duel in progress).
    static bool IsActiveFor(Player* bot);

    // Legacy scripted movers masked in duels while the channel is enabled (DEC-036).
    static bool IsLegacyMovementAction(std::string const& name);

    // ServerFacade::SetFacingTo detour. Returns true when the executor owns facing for this bot
    // right now (including jump-turn initiation); false falls through to the legacy instant turn.
    bool HandleExternalFacing(Player* bot, WorldObject* target);

    // Shared with the CF_MOVE feature pack: 8 foe-relative walkability probes (1 = walkable).
    // Recomputes when stale (older than the subtick), so feature rows are complete even when
    // the executor is not running this bot.
    void GetProbes(Player* bot, Unit* foe, float out[8]);

    // Log-only duel_v5 columns.
    uint8 GetIntent(ObjectGuid guid) const;
    uint8 GetExpertIntent(ObjectGuid guid) const;
    float GetRealizedHeading(ObjectGuid guid) const;

private:
    MlDuelMovement() = default;

    void UpdateBot(PlayerbotAI* botAI, MlBotMovementState& state, uint32 dtMs);
    void ContinueJump(Player* bot, MlBotMovementState& state, uint32 dtMs);
    void StartJumpTurn(Player* bot, MlBotMovementState& state, float newFacing);
    void EnsureStopped(Player* bot, MlBotMovementState& state);
    // Returns false when the packet cannot be dispatched (no session / live movespline).
    bool SendMovePacket(Player* bot, MlBotMovementState& state, uint16 opcode, uint32 moveFlags,
                        float x, float y, float z, float o, uint32 fallTime = 0, bool withJump = false,
                        float jumpDir = 0.0f, float jumpSpeedXY = 0.0f);
    // Server-side position/orientation update with no client broadcast (between heartbeats).
    void SilentRelocate(Player* bot, float x, float y, float z, float o);
    void ComputeProbes(Player* bot, Unit* foe, MlBotMovementState& state);

    MlBotMovementState* GetState(ObjectGuid guid, bool create);
    MlBotMovementState const* FindState(ObjectGuid guid) const;
    void EraseState(ObjectGuid guid);

    mutable std::mutex mtx;
    std::unordered_map<uint32, MlBotMovementState> states;
};

#define sMlDuelMovement MlDuelMovement::instance()

#endif
