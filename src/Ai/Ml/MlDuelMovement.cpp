/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 * DEC-036 M0 movement substrate: packet-driven intent executor + scripted intent movers.
 *
 * Motion mechanism: synthesized client movement packets (MSG_MOVE_START_* / HEARTBEAT / STOP)
 * dispatched synchronously through the bot's own WorldSession from the bot's map-update thread —
 * the same thread PROCESS_THREADSAFE client movement runs on. Server relay, fall handling and
 * m_movementInfo state all come from the normal movement handler. No MotionMaster splines on
 * this channel: a live spline makes VerifyMovementInfo drop every packet, so the executor backs
 * off whenever one is active (charge, knockback, fear) and resumes when it finalizes.
 */

#include "MlDuelMovement.h"

#include <cmath>
#include <unordered_set>

#include "GameObject.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Spell.h"
#include "SpellAuraDefines.h"
#include "Timer.h"
#include "Unit.h"
#include "WorldPacket.h"
#include "WorldSession.h"

namespace
{
constexpr float kBaseRunSpeed = 7.0f;        // playerBaseMoveSpeed[MOVE_RUN]
constexpr float kJumpVelocity = 7.9555f;     // client jump up-velocity
constexpr float kGravity = 19.2911f;         // Movement::gravity
constexpr float kCastArcMargin = 1.22173f;   // 70 deg: 20-deg margin inside HasInArc(pi) (DEC-036)
constexpr float kProbeHeightDelta = 2.0f;    // > 2 y step/drop = invalid (DEC-036)
constexpr float kArbiterLeashYd = 35.0f;     // stay inside the duel-flag out-of-bounds radius
constexpr float kKiteRetreatYd = 15.0f;      // Frost: retreat below this (DEC-036)
constexpr float kKiteMaxYd = 30.0f;          // Frost: approach beyond this / bank distance until this

float NormalizeRel(float a)
{
    while (a > float(M_PI))
        a -= 2.0f * float(M_PI);
    while (a < -float(M_PI))
        a += 2.0f * float(M_PI);
    return a;
}

struct IntentDecision
{
    uint8 intent = ML_MOVE_INTENT_HOLD;
    // Foe safely impaired: retreat is a dead-away sprint (turn-and-run) instead of strafe-away.
    bool sprint = false;
};

bool FoeMovementImpaired(Unit* foe)
{
    return foe->IsRooted() || foe->HasUnitState(UNIT_STATE_ROOT) ||
           foe->GetMaxNegativeAuraModifier(SPELL_AURA_MOD_DECREASE_SPEED) < 0;
}

// M0 scripted intent policies (DEC-036). The scripted pick doubles as the expert label.
IntentDecision ComputeScriptedIntent(Player* bot, Unit* foe)
{
    if (bot->getClass() == CLASS_MAGE)
    {
        float const dist = bot->GetDistance(foe);
        if (!bot->IsWithinLOSInMap(foe) || dist > kKiteMaxYd)
            return {ML_MOVE_INTENT_TOWARD, false};
        if (FoeMovementImpaired(foe) && dist < kKiteMaxYd)
            return {ML_MOVE_INTENT_AWAY, true};
        if (dist < kKiteRetreatYd)
            return {ML_MOVE_INTENT_AWAY, false};
        return {ML_MOVE_INTENT_HOLD, false};
    }

    // Arms chase (and default for any other class): toward foe out of melee, hold in melee.
    if (bot->IsWithinMeleeRange(foe))
        return {ML_MOVE_INTENT_HOLD, false};
    return {ML_MOVE_INTENT_TOWARD, false};
}
}  // namespace

MlDuelMovement& MlDuelMovement::instance()
{
    static MlDuelMovement inst;
    return inst;
}

bool MlDuelMovement::IsActiveFor(Player* bot)
{
    return bot && sPlayerbotAIConfig.mlDuelMovementEnable && bot->duel && bot->duel->Opponent &&
           bot->duel->State == DUEL_STATE_IN_PROGRESS;
}

bool MlDuelMovement::IsLegacyMovementAction(std::string const& name)
{
    static std::unordered_set<std::string> const masked = {
        "reach melee",     "reach spell",     "flee",        "flee with pet",
        "runaway",         "combat formation move",          "set facing",
        "set behind",      "avoid aoe",       "tank face",   "rear flank",
        "disperse set",    "move out of enemy contact",      "move random",
        "move from group",
    };
    return masked.count(name) != 0;
}

MlBotMovementState* MlDuelMovement::GetState(ObjectGuid guid, bool create)
{
    std::lock_guard<std::mutex> lock(mtx);
    if (create)
        return &states[guid.GetCounter()];
    auto it = states.find(guid.GetCounter());
    return it != states.end() ? &it->second : nullptr;
}

MlBotMovementState const* MlDuelMovement::FindState(ObjectGuid guid) const
{
    std::lock_guard<std::mutex> lock(mtx);
    auto it = states.find(guid.GetCounter());
    return it != states.end() ? &it->second : nullptr;
}

void MlDuelMovement::EraseState(ObjectGuid guid)
{
    std::lock_guard<std::mutex> lock(mtx);
    states.erase(guid.GetCounter());
}

void MlDuelMovement::Update(PlayerbotAI* botAI, uint32 elapsed)
{
    if (!botAI)
        return;

    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported() || bot->IsDuringRemoveFromWorld())
        return;

    if (!IsActiveFor(bot))
    {
        if (MlBotMovementState* state = GetState(bot->GetGUID(), false))
        {
            // Duel over: hand movement back to the legacy movers cleanly.
            if ((state->moving || state->airborne) && bot->IsAlive())
                EnsureStopped(bot, *state);
            EraseState(bot->GetGUID());
        }
        return;
    }

    MlBotMovementState* state = GetState(bot->GetGUID(), true);
    state->accumMs += elapsed;
    uint32 const subtick = std::max<uint32>(50, sPlayerbotAIConfig.mlDuelMovementSubtickMs);
    if (state->accumMs < subtick)
        return;

    // Cap dt after long stalls so one subtick can never integrate a teleport-sized step.
    uint32 const dt = std::min<uint32>(state->accumMs, 400);
    state->accumMs = 0;
    UpdateBot(botAI, *state, dt);
}

void MlDuelMovement::UpdateBot(PlayerbotAI* botAI, MlBotMovementState& state, uint32 dtMs)
{
    Player* bot = botAI->GetBot();
    Unit* foe = bot->duel->Opponent;
    if (!foe || !foe->IsInWorld() || foe->GetMapId() != bot->GetMapId())
        return;

    float const dtSec = dtMs / 1000.0f;

    if (state.airborne)
    {
        ContinueJump(bot, state, dtMs);
        return;
    }

    // External motion (charge, knockback, fear) owns movement while its spline runs.
    if (!bot->movespline->Finalized())
    {
        state.moving = false;
        state.moveFlags = 0;
        state.suppressed = false;
        return;
    }

    if (!bot->IsAlive() || bot->IsRooted() || bot->HasUnitState(UNIT_STATE_ROOT) ||
        bot->HasUnitState(UNIT_STATE_LOST_CONTROL))
    {
        // Root enforcement / control loss: the server already stopped us; packets would be rejected.
        state.moving = false;
        state.moveFlags = 0;
        return;
    }

    // Hardcasts and channels suppress movement (DEC-036); the cast pipeline owns facing meanwhile.
    Spell* generic = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    bool const casting = (generic && generic->getState() == SPELL_STATE_PREPARING) ||
                         bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
    if (casting)
    {
        EnsureStopped(bot, state);
        state.suppressed = true;
        return;
    }
    state.suppressed = false;

    float const bearing = bot->GetAngle(foe);
    IntentDecision dec = ComputeScriptedIntent(bot, foe);
    // Debounce: a changed intent must persist one extra subtick before it takes effect, so
    // range-boundary jitter (melee reach, kite bands) cannot flap START/STOP broadcasts.
    if (dec.intent != state.intent)
    {
        if (dec.intent == state.pendingIntent && state.pendingCount >= 1)
        {
            state.pendingCount = 0;
        }
        else
        {
            state.pendingIntent = dec.intent;
            state.pendingCount = 1;
            dec.intent = state.intent;
        }
    }
    else
    {
        state.pendingCount = 0;
    }
    state.intent = dec.intent;
    state.expertIntent = dec.intent;

    uint32 const nowMs = getMSTime();
    bool const throttle = sPlayerbotAIConfig.mlDuelMovementThrottleBroadcast;
    auto faceFoe = [&]()
    {
        float const off = std::fabs(NormalizeRel(bot->GetOrientation() - bearing));
        if (off <= 0.15f)
            return;
        // Throttled: broadcast a facing packet at most ~3/s, correct silently in between.
        if (!throttle || (off > 0.25f && getMSTimeDiff(state.lastPacketMs, nowMs) >= 300))
            SendMovePacket(bot, state, MSG_MOVE_SET_FACING, 0, bot->GetPositionX(), bot->GetPositionY(),
                           bot->GetPositionZ(), bearing);
        else
            SilentRelocate(bot, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bearing);
    };

    if (dec.intent == ML_MOVE_INTENT_HOLD)
    {
        // No step, no clamp: probes stay cold while holding (features refresh them on demand).
        EnsureStopped(bot, state);
        faceFoe();
        state.realizedHeading = 0.0f;
        return;
    }

    ComputeProbes(bot, foe, state);

    // Safety clamp: slide an invalid step onto the nearest valid 45-degree neighbor; hold if none.
    int const wanted = dec.intent - 1;
    static int const slideOrder[8] = {0, 1, -1, 2, -2, 3, -3, 4};
    int resolved = -1;
    for (int step : slideOrder)
    {
        int const idx = ((wanted + step) % 8 + 8) % 8;
        if (state.probes[idx] > 0.5f)
        {
            resolved = idx;
            break;
        }
    }
    if (resolved < 0)
    {
        EnsureStopped(bot, state);
        faceFoe();
        return;
    }

    float moveDir = Position::NormalizeOrientation(bearing + resolved * float(M_PI_4));
    float facing;
    uint32 moveFlags;

    bool const retreat = resolved >= 3 && resolved <= 5;
    if (retreat && !dec.sprint && bot->getClass() == CLASS_MAGE)
    {
        // Strafe-away: face the foe at the 70-deg cast-arc edge, strafe out at full speed
        // (~160 deg off the foe bearing). Casts stay available the whole time.
        int8 side = state.strafeSide;
        if (resolved == 3)
            side = 1;
        else if (resolved == 5)
            side = -1;
        state.strafeSide = side;
        facing = Position::NormalizeOrientation(bearing + side * kCastArcMargin);
        moveDir = Position::NormalizeOrientation(facing + side * float(M_PI_2));
        moveFlags = side > 0 ? MOVEMENTFLAG_STRAFE_LEFT : MOVEMENTFLAG_STRAFE_RIGHT;
    }
    else
    {
        // Turn-and-run (approach, chase, snare-window sprint).
        facing = moveDir;
        moveFlags = MOVEMENTFLAG_FORWARD;
    }

    float const speed = bot->GetSpeed(MOVE_RUN);
    float const nx = bot->GetPositionX() + std::cos(moveDir) * speed * dtSec;
    float const ny = bot->GetPositionY() + std::sin(moveDir) * speed * dtSec;

    // Don't kite past the duel-flag leash: out-of-bounds forfeits the duel and poisons the farm.
    if (retreat)
    {
        if (GameObject* arbiter = ObjectAccessor::GetGameObject(*bot, bot->GetGuidValue(PLAYER_DUEL_ARBITER)))
        {
            if (arbiter->GetDistance2d(nx, ny) > kArbiterLeashYd &&
                arbiter->GetDistance2d(nx, ny) > arbiter->GetDistance2d(bot))
            {
                EnsureStopped(bot, state);
                faceFoe();
                return;
            }
        }
    }

    float const groundZ = bot->GetMapHeight(nx, ny, bot->GetPositionZ() + 2.0f);
    if (std::fabs(groundZ - bot->GetPositionZ()) > 2.5f)
    {
        EnsureStopped(bot, state);
        return;
    }

    // Observers extrapolate steady motion from the move flags, so only state changes and a
    // ~500 ms heartbeat need broadcasting; between them the position integrates silently.
    // This keeps per-bot packet rate at real-client levels instead of one per subtick.
    bool const stateChanged = !state.moving || state.moveFlags != moveFlags ||
                              std::fabs(NormalizeRel(facing - state.facing)) > 0.35f;
    if (!throttle || stateChanged || getMSTimeDiff(state.lastPacketMs, nowMs) >= 500)
    {
        uint16 opcode = MSG_MOVE_HEARTBEAT;
        if (!state.moving || state.moveFlags != moveFlags)
            opcode = (moveFlags & MOVEMENTFLAG_STRAFE_LEFT)    ? MSG_MOVE_START_STRAFE_LEFT
                     : (moveFlags & MOVEMENTFLAG_STRAFE_RIGHT) ? MSG_MOVE_START_STRAFE_RIGHT
                                                               : MSG_MOVE_START_FORWARD;
        if (!SendMovePacket(bot, state, opcode, moveFlags, nx, ny, groundZ, facing))
            return;
    }
    else
    {
        SilentRelocate(bot, nx, ny, groundZ, facing);
        state.facing = facing;
    }
    state.moving = true;
    state.moveFlags = moveFlags;
    state.realizedHeading = NormalizeRel(moveDir - bearing);
}

void MlDuelMovement::ContinueJump(Player* bot, MlBotMovementState& state, uint32 dtMs)
{
    state.jumpElapsedMs += dtMs;
    float const t = state.jumpElapsedMs / 1000.0f;
    float const landT = 2.0f * kJumpVelocity / kGravity;
    float const dtSec = dtMs / 1000.0f;
    float const nx = bot->GetPositionX() + std::cos(state.jumpDirWorld) * state.jumpSpeedXY * dtSec;
    float const ny = bot->GetPositionY() + std::sin(state.jumpDirWorld) * state.jumpSpeedXY * dtSec;

    if (t >= landT)
    {
        float groundZ = bot->GetMapHeight(nx, ny, state.jumpStartZ + 2.0f);
        if (std::fabs(groundZ - state.jumpStartZ) > 5.0f)
            groundZ = state.jumpStartZ;
        float const o = state.restoreFacingOnLand ? state.facingAfterLand : bot->GetOrientation();
        SendMovePacket(bot, state, MSG_MOVE_FALL_LAND, state.moveFlags, nx, ny, groundZ, o,
                       state.jumpElapsedMs);
        state.airborne = false;
        state.restoreFacingOnLand = false;
        return;
    }

    // Airborne preserves the velocity vector (DEC-036); intents cannot pre-empt the jump.
    // Observers extrapolate the parabola from the takeoff packet, so mid-air updates stay silent
    // except one corrective heartbeat.
    float const zOff = kJumpVelocity * t - 0.5f * kGravity * t * t;
    float const z = state.jumpStartZ + std::max(0.0f, zOff);
    if (!sPlayerbotAIConfig.mlDuelMovementThrottleBroadcast ||
        getMSTimeDiff(state.lastPacketMs, getMSTime()) >= 400)
        SendMovePacket(bot, state, MSG_MOVE_HEARTBEAT, state.moveFlags | MOVEMENTFLAG_FALLING, nx, ny, z,
                       bot->GetOrientation(), state.jumpElapsedMs, true, state.jumpDirWorld,
                       state.jumpSpeedXY);
    else
        SilentRelocate(bot, nx, ny, z, bot->GetOrientation());
}

void MlDuelMovement::StartJumpTurn(Player* bot, MlBotMovementState& state, float /*newFacing*/)
{
    if (state.airborne || !state.moving)
        return;

    float const dir = bot->GetOrientation();
    // Takeoff keeps the pre-flip facing; the caller flips orientation right after, and the next
    // airborne heartbeat carries it. Facing is restored at landing (atomic until landing).
    if (!SendMovePacket(bot, state, MSG_MOVE_JUMP, state.moveFlags | MOVEMENTFLAG_FALLING,
                        bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), dir, 0, true, dir,
                        bot->GetSpeed(MOVE_RUN)))
        return;

    state.airborne = true;
    state.jumpElapsedMs = 0;
    state.jumpDirWorld = dir;
    state.jumpSpeedXY = bot->GetSpeed(MOVE_RUN);
    state.jumpStartZ = bot->GetPositionZ();
    state.restoreFacingOnLand = true;
    state.facingAfterLand = dir;
}

bool MlDuelMovement::HandleExternalFacing(Player* bot, WorldObject* target)
{
    if (!bot || !target || !IsActiveFor(bot) || target != bot->duel->Opponent)
        return false;

    MlBotMovementState* state = GetState(bot->GetGUID(), false);
    if (!state)
        return false;

    if (state->airborne)
    {
        // Mid jump-turn: allow the flip; landing restores the run facing.
        bot->SetOrientation(bot->GetAngle(target));
        bot->SendMovementFlagUpdate(true);
        return true;
    }

    if (!state->moving || state->suppressed)
        return false;  // stationary: the legacy instant turn is fine

    if (bot->HasInArc(static_cast<float>(M_PI), target))
        return true;  // cast arc already satisfied — no turn needed

    // Dead-away sprint with an instant queued: atomic jump-turn (DEC-036).
    StartJumpTurn(bot, *state, bot->GetAngle(target));
    if (state->airborne)
    {
        bot->SetOrientation(bot->GetAngle(target));
        bot->SendMovementFlagUpdate(true);
        return true;
    }
    return false;
}

void MlDuelMovement::GetProbes(Player* bot, Unit* foe, float out[8])
{
    for (int i = 0; i < 8; ++i)
        out[i] = 0.0f;
    if (!bot || !foe)
        return;

    MlBotMovementState* state = GetState(bot->GetGUID(), true);
    uint32 const subtick = std::max<uint32>(50, sPlayerbotAIConfig.mlDuelMovementSubtickMs);
    if (getMSTimeDiff(state->probeStampMs, getMSTime()) > subtick)
        ComputeProbes(bot, foe, *state);
    for (int i = 0; i < 8; ++i)
        out[i] = state->probes[i];
}

void MlDuelMovement::ComputeProbes(Player* bot, Unit* foe, MlBotMovementState& state)
{
    float const bearing = bot->GetAngle(foe);
    float const range = std::max(1.0f, sPlayerbotAIConfig.mlDuelMovementProbeRangeYd);
    float const bx = bot->GetPositionX();
    float const by = bot->GetPositionY();
    float const bz = bot->GetPositionZ();

    // DEC-036 throughput gate remediation: refresh 4 of the 8 directions per pass (alternating
    // halves), halving vmap query load; any slot is at most ~2 subticks stale.
    for (int k = state.probePhase; k < 8; k += 2)
    {
        float const ang = bearing + k * float(M_PI_4);
        float const px = bx + std::cos(ang) * range;
        float const py = by + std::sin(ang) * range;
        float const pz = bot->GetMapHeight(px, py, bz + 2.0f);
        bool const valid = std::fabs(pz - bz) <= kProbeHeightDelta && bot->IsWithinLOS(px, py, pz + 2.0f);
        state.probes[k] = valid ? 1.0f : 0.0f;
    }
    state.probePhase ^= 1;
    state.probeStampMs = getMSTime();
}

uint8 MlDuelMovement::GetIntent(ObjectGuid guid) const
{
    MlBotMovementState const* state = FindState(guid);
    return state ? state->intent : uint8(ML_MOVE_INTENT_HOLD);
}

uint8 MlDuelMovement::GetExpertIntent(ObjectGuid guid) const
{
    MlBotMovementState const* state = FindState(guid);
    return state ? state->expertIntent : uint8(ML_MOVE_INTENT_HOLD);
}

float MlDuelMovement::GetRealizedHeading(ObjectGuid guid) const
{
    MlBotMovementState const* state = FindState(guid);
    return state ? state->realizedHeading : 0.0f;
}

void MlDuelMovement::EnsureStopped(Player* bot, MlBotMovementState& state)
{
    bool const wasMoving = state.moving || state.airborne;
    state.moving = false;
    state.airborne = false;
    state.moveFlags = 0;
    if (!wasMoving)
        return;
    if (bot->IsRooted() || bot->HasUnitState(UNIT_STATE_ROOT))
        return;  // server-side root already stopped us; a rootless packet would be rejected
    SendMovePacket(bot, state, MSG_MOVE_STOP, 0, bot->GetPositionX(), bot->GetPositionY(),
                   bot->GetPositionZ(), bot->GetOrientation());
}

bool MlDuelMovement::SendMovePacket(Player* bot, MlBotMovementState& state, uint16 opcode, uint32 moveFlags,
                                    float x, float y, float z, float o, uint32 fallTime, bool withJump,
                                    float jumpDir, float jumpSpeedXY)
{
    WorldSession* session = bot->GetSession();
    if (!session)
        return false;
    if (!bot->movespline->Finalized())
        return false;

    o = Position::NormalizeOrientation(o);

    WorldPacket data(opcode, 64);
    data << bot->GetGUID().WriteAsPacked();
    data << uint32(moveFlags);
    data << uint16(0);  // extra movement flags
    data << uint32(getMSTime());
    data << float(x) << float(y) << float(z) << float(o);
    data << uint32(fallTime);
    if (withJump)  // MOVEMENTFLAG_FALLING serializes the jump block
    {
        data << float(-kJumpVelocity);  // client convention: negative = upward
        data << float(std::sin(jumpDir));
        data << float(std::cos(jumpDir));
        data << float(jumpSpeedXY);
    }

    session->HandleMovementOpcodes(data);
    state.facing = o;
    state.lastPacketMs = getMSTime();
    return true;
}

void MlDuelMovement::SilentRelocate(Player* bot, float x, float y, float z, float o)
{
    if (!bot->movespline->Finalized())
        return;
    o = Position::NormalizeOrientation(o);
    bot->UpdatePosition(x, y, z, o);
    bot->m_movementInfo.pos.Relocate(x, y, z, o);
    bot->m_movementInfo.time = getMSTime();
}
