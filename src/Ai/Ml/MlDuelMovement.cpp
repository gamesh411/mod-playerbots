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
 *
 * Observer transport (AiPlayerbot.MlDuelMovementTransport): "packets" is the default (DEC-048) —
 * the DEC-036 MSG_MOVE_* wire, i.e. the opcodes a real client sends, so observers render genuine
 * movement animation. "spline" is the DEC-045 alternative: broadcast-only SMSG_MONSTER_MOVE
 * synthesis, one self-anchoring segment per intent horizon, facing-angle mode, parabolic jump
 * arcs, anchoring stop-spline on halt/root/death. Either way the choice is wire-only: server-side
 * stepping, flags and clamps are identical, so frozen M0/M1 stages stay valid.
 * Spline was introduced as a workaround for the #27 observer freeze under a flag-extrapolation
 * theory. DEC-046 falsified that theory, DEC-047 (below) found the real cause, and packets soaked
 * clean afterwards — so spline is now a rollback path, not a mitigation, and is a live candidate
 * for removal.
 *
 * DEC-047 observer freeze: the client's per-frame movement stepper loops until the steps it takes
 * cover the frame, and only honours MOVEMENTFLAG_ROOT for a unit carrying no moving or falling
 * flag. ROOT beside FORWARD/STRAFE/FALLING therefore steps zero ms forever and hangs the client's
 * main thread. Executor-owned flags must never outlive the state that justified them, and a root
 * must abandon a jump in flight rather than ride it out — see UpdateBot and EnsureStopped.
 */

#include "MlDuelMovement.h"

#include <atomic>
#include <cmath>
#include <unordered_set>

#include "DetourExtended.h"
#include "GameObject.h"
#include "Map.h"
#include "MapCollisionData.h"
#include "MlDecisionLogger.h"
#include "MlScorer.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "MoveSplineFlag.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "Random.h"
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

// DEC-045 spline transport: segments predict one intent horizon ahead (the 500 ms cadence the
// packet transport heartbeats at), re-anchoring at server truth by construction every send.
constexpr uint32 kSegmentHorizonMs = 500;
// SMSG_MONSTER_MOVE type bytes (Movement::PacketBuilder MonsterMoveType).
constexpr uint8 kMonsterMoveStop = 1;
constexpr uint8 kMonsterMoveFacingAngle = 4;
// Synthesized wire-only spline ids; high base keeps them clear of the core's MoveSplineInit ids.
std::atomic<uint32> splineIdGen{0x71000000};

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

// A candidate point must sit on the navmesh: raw height+LoS probing lets 100ms steps
// stair-climb onto props (tree trunks, siege equipment) that pathing would never enter,
// leaving kiters unreachable by a melee chaser. Point query only - no path is computed.
bool OnNavMesh(Player* bot, float x, float y, float z)
{
    dtNavMeshQuery const* query = bot->GetMap()->GetMapCollisionData().GetMMapData().GetNavMeshQuery();
    if (!query)
        return true;  // no mmaps loaded here: fall back to the height+LoS-only behavior

    float const location[3] = {y, z, x};  // recast coordinate order
    float const extents[3] = {1.5f, 2.5f, 1.5f};
    dtQueryFilterExt filter;
    dtPolyRef ref = 0;
    float nearest[3] = {0.0f, 0.0f, 0.0f};
    if (dtStatusFailed(query->findNearestPoly(location, extents, &filter, &ref, nearest)))
        return false;
    // The poly must be at the candidate's height: a poly far below (ground under a prop
    // the candidate is standing on) must not validate the perch.
    return ref != 0 && std::fabs(nearest[1] - z) <= 2.0f;
}

// Softmax over the 9 intent logits; tau <= 0 is argmax (DEC-039 farm/demo semantics).
uint8 SampleIntentIndex(float const* logits, size_t n, float tau)
{
    size_t best = 0;
    float maxLogit = logits[0];
    for (size_t i = 1; i < n; ++i)
        if (logits[i] > maxLogit)
        {
            maxLogit = logits[i];
            best = i;
        }
    if (tau <= 0.0f)
        return static_cast<uint8>(best);

    float sumWeight = 0.0f;
    float weights[ML_MOVE_INTENT_COUNT];
    for (size_t i = 0; i < n; ++i)
    {
        weights[i] = std::exp((logits[i] - maxLogit) / tau);
        sumWeight += weights[i];
    }
    float const roll = frand(0.0f, sumWeight);
    float cumulative = 0.0f;
    for (size_t i = 0; i < n; ++i)
    {
        cumulative += weights[i];
        if (roll <= cumulative)
            return static_cast<uint8>(i);
    }
    return static_cast<uint8>(n - 1);
}

// DEC-039 M1 ranker pick. False (missing/mis-shaped model, no context) falls back to scripted.
bool RankerIntent(PlayerbotAI* botAI, Player* bot, Unit* foe, uint8& outIntent)
{
    AiObjectContext* context = botAI->GetAiObjectContext();
    if (!context)
        return false;
    auto* featureValue = context->GetValue<CombatFeatureVector>("combat decision features");
    if (!featureValue)
        return false;

    float logits[ML_MOVE_INTENT_COUNT];
    CombatFeatureVector const features = featureValue->Get();
    if (!sMlScorer.ScoreMovement(bot->getClass(), features, logits, ML_MOVE_INTENT_COUNT))
        return false;

    // Sparring vs a real player always plays argmax, mirroring the ability-channel demo rule.
    float tau = sPlayerbotAIConfig.mlDuelMovementSoftmaxTemperature;
    if (Player* foePlayer = foe->ToPlayer())
    {
        PlayerbotAI* foeAI = GET_PLAYERBOT_AI(foePlayer);
        if (!foeAI || IsSelfBot(foePlayer))
            tau = 0.0f;
    }

    outIntent = SampleIntentIndex(logits, ML_MOVE_INTENT_COUNT, tau);
    return true;
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
            // Duel over: hand movement back to the legacy movers cleanly. This must run for
            // dead bots too - flags that outlive the executor on a corpse have no client to
            // clear them, and a revived bot with wedged isMoving() drops out of the park
            // patrol/pairing pool forever (EnsureStopped strips flags first, packets only
            // when alive).
            if (state->moving || state->airborne)
                EnsureStopped(bot, *state);
            EraseState(bot->GetGUID());
            // Legacy movers broadcast their motion themselves (MotionMaster splines);
            // the wire mask must not outlive the executor.
            bot->CustomData.Erase("mlDuelWireMoveFlagMask");
        }
        return;
    }

    MlBotMovementState* state = GetState(bot->GetGUID(), true);
    // DEC-045: under spline transport, core masks this bot's directional+falling move flags in
    // every observer-facing serialization (create block, heartbeat, teleport) - the #27 livelock
    // is fed by flag extrapolation, and the world-entry create block otherwise still carries the
    // executor's flags to fresh observers. Wire-only; server-side flags stay the frozen truth.
    if (sPlayerbotAIConfig.mlDuelMovementSplineTransport)
        bot->CustomData.GetDefault<DataMap::Base>("mlDuelWireMoveFlagMask");
    else
        bot->CustomData.Erase("mlDuelWireMoveFlagMask");
    state->accumMs += elapsed;
    uint32 const subtick = std::max<uint32>(50, sPlayerbotAIConfig.mlDuelMovementSubtickMs);
    if (state->accumMs < subtick)
        return;

    // Cap dt after long stalls so one subtick can never integrate a teleport-sized step.
    uint32 const dt = std::min<uint32>(state->accumMs, 400);
    state->accumMs = 0;
    UpdateBot(botAI, *state, dt);

    // DEC-039 movement CSV: every Nth decided subtick (500ms intent horizon at N=5) plus on
    // every intent change; rewards + terminal resolve at duel end in the logger.
    if (state->decided && sPlayerbotAIConfig.mlLoggingEnabled)
    {
        ++state->sinceLog;
        uint32 const every = std::max<uint32>(1, sPlayerbotAIConfig.mlDuelMovementLogEveryNSubticks);
        if (state->sinceLog >= every || state->intent != state->lastLoggedIntent)
        {
            sMlDecisionLogger.LogMovementRow(botAI, state->intent, state->expertIntent, state->realizedHeading);
            state->sinceLog = 0;
            state->lastLoggedIntent = state->intent;
        }
    }
}

void MlDuelMovement::UpdateBot(PlayerbotAI* botAI, MlBotMovementState& state, uint32 dtMs)
{
    Player* bot = botAI->GetBot();
    Unit* foe = bot->duel->Opponent;
    state.decided = false;
    if (!foe || !foe->IsInWorld() || foe->GetMapId() != bot->GetMapId())
    {
        // The duel is ending; hand movement back now rather than leaving executor flags on a
        // unit no subtick will visit again before the duel-over sweep in Update().
        EnsureStopped(bot, state);
        return;
    }

    float const dtSec = dtMs / 1000.0f;

    // Executor-owned directional flags must not linger when we yield control (isMoving() gates
    // legacy pathing); FALLING is left alone here — knockback/fall handling owns it.
    auto yieldMoveFlags = [&]()
    {
        state.moving = false;
        state.moveFlags = 0;
        bot->m_movementInfo.RemoveMovementFlag(MOVEMENTFLAG_FORWARD | MOVEMENTFLAG_BACKWARD |
                                               MOVEMENTFLAG_STRAFE_LEFT | MOVEMENTFLAG_STRAFE_RIGHT);
    };

    // External motion (charge, knockback, fear) owns movement while its spline runs.
    if (!bot->movespline->Finalized())
    {
        yieldMoveFlags();
        // The core's own monster-move packet owns the wire too; an anchor here would cancel it
        // client-side, so just note our segment is no longer what observers are playing.
        state.wireMoving = false;
        state.suppressed = false;
        return;
    }

    // DEC-047: this must be decided before the airborne branch. The 3.3.5 client's per-frame
    // movement integrator (`while (consumed < elapsed) consumed += Step()`) only consults ROOT
    // when the unit carries no moving or falling flag; with both present the step advances zero
    // ms and the loop never exits, hanging the observer's main thread (#27/#31). Riding a jump
    // through an incoming root produces exactly that pair, so the jump is abandoned instead.
    if (!bot->IsAlive() || bot->IsRooted() || bot->HasUnitState(UNIT_STATE_ROOT) ||
        bot->HasUnitState(UNIT_STATE_LOST_CONTROL))
    {
        // Root enforcement / control loss: the server already stopped us; packets would be
        // rejected. EnsureStopped strips the executor's flags — FALLING included — and anchors
        // observers at server truth with a broadcast-only stop-spline, which is never rejected.
        EnsureStopped(bot, state);
        return;
    }

    if (state.airborne)
    {
        ContinueJump(bot, state, dtMs);
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
    // The scripted mover runs every subtick as the DAgger teacher (DEC-039), even while the
    // ranker drives; its raw pick is the expert_movement_intent label.
    uint8 const teacherIntent = dec.intent;
    if (sPlayerbotAIConfig.mlDuelMovementPolicy == "ranker")
    {
        uint8 rankerPick = ML_MOVE_INTENT_HOLD;
        if (RankerIntent(botAI, bot, foe, rankerPick))
        {
            dec.intent = rankerPick;
            // Execution mechanics stay policy-independent: retreat picks sprint (turn-and-run)
            // under the same snare-window condition the scripted kite uses.
            bool const retreatPick =
                rankerPick >= ML_MOVE_INTENT_AWAY_LEFT && rankerPick <= ML_MOVE_INTENT_AWAY_RIGHT;
            dec.sprint = retreatPick && FoeMovementImpaired(foe) && bot->GetDistance(foe) < kKiteMaxYd;
        }
    }
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
    state.expertIntent = teacherIntent;
    state.decided = true;

    uint32 const nowMs = getMSTime();
    bool const throttle = sPlayerbotAIConfig.mlDuelMovementThrottleBroadcast;
    bool const spline = sPlayerbotAIConfig.mlDuelMovementSplineTransport;
    auto faceFoe = [&]()
    {
        float const off = std::fabs(NormalizeRel(bot->GetOrientation() - bearing));
        if (off <= 0.15f)
            return;
        // Throttled: broadcast a facing update at most ~3/s, correct silently in between.
        bool const broadcast = sPlayerbotAIConfig.mlDuelMovementFacingPackets &&
                               (!throttle || (off > 0.25f && getMSTimeDiff(state.lastPacketMs, nowMs) >= 300));
        if (spline)
        {
            // Server truth first; stationary turns render as zero-length facing segments.
            SilentRelocate(bot, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bearing);
            if (broadcast)
                BroadcastSplineSegment(bot, state, bot->GetPositionX(), bot->GetPositionY(),
                                       bot->GetPositionZ(), bearing, 100);
        }
        else if (broadcast)
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
        state.realizedHeading = 0.0f;
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
                state.realizedHeading = 0.0f;
                return;
            }
        }
    }

    float const groundZ = bot->GetMapHeight(nx, ny, bot->GetPositionZ() + 2.0f);
    if (std::fabs(groundZ - bot->GetPositionZ()) > 2.5f || !OnNavMesh(bot, nx, ny, groundZ))
    {
        EnsureStopped(bot, state);
        state.realizedHeading = 0.0f;
        return;
    }

    // Only state changes and a ~500 ms cadence need broadcasting; between them the position
    // integrates silently. This keeps per-bot packet rate at real-client levels instead of one
    // per subtick (packets: observers extrapolate from move flags; spline: the segment plays out).
    bool const stateChanged = !state.moving || state.moveFlags != moveFlags ||
                              std::fabs(NormalizeRel(facing - state.facing)) > 0.35f;
    bool const broadcast =
        !throttle || stateChanged || getMSTimeDiff(state.lastPacketMs, nowMs) >= kSegmentHorizonMs;
    if (spline)
    {
        if (!ApplyServerMoveState(bot, moveFlags, nx, ny, groundZ, facing))
            return;
        if (broadcast)
        {
            // Self-anchoring segment: start at the truth just applied, end at the obstacle-clamped
            // prediction one intent horizon out. A segment must not cross geometry its endpoints
            // straddle (observers integrate the straight line through it), so the full-length
            // prediction also validates its midpoint; failure halves the horizon, then anchors.
            float const horizonSec = kSegmentHorizonMs / 1000.0f;
            float px = nx + std::cos(moveDir) * speed * horizonSec;
            float py = ny + std::sin(moveDir) * speed * horizonSec;
            float const mx = nx + std::cos(moveDir) * speed * horizonSec * 0.5f;
            float const my = ny + std::sin(moveDir) * speed * horizonSec * 0.5f;
            float const mz = bot->GetMapHeight(mx, my, groundZ + 2.0f);
            bool const midValid = std::fabs(mz - groundZ) <= 2.5f && OnNavMesh(bot, mx, my, mz);
            float pz = bot->GetMapHeight(px, py, groundZ + 2.0f);
            uint32 durationMs = kSegmentHorizonMs;
            if (!midValid || std::fabs(pz - groundZ) > 2.5f || !OnNavMesh(bot, px, py, pz))
            {
                px = mx;
                py = my;
                pz = mz;
                durationMs = kSegmentHorizonMs / 2;
                if (!midValid)
                {
                    px = nx;
                    py = ny;
                    pz = groundZ;
                    durationMs = 100;
                }
            }
            BroadcastSplineSegment(bot, state, px, py, pz, facing, durationMs);
            state.wireMoving = px != nx || py != ny;
        }
        else
            state.facing = facing;
    }
    else if (broadcast)
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
        if (sPlayerbotAIConfig.mlDuelMovementSplineTransport)
        {
            // The takeoff segment already played the whole arc; the restored facing rides the
            // next ground segment (the horizon timer has expired by landing), so no packet here.
            if (ApplyServerMoveState(bot, state.moveFlags, nx, ny, groundZ, o))
                bot->m_movementInfo.fallTime = state.jumpElapsedMs;
        }
        else
            SendMovePacket(bot, state, MSG_MOVE_FALL_LAND, state.moveFlags, nx, ny, groundZ, o,
                           state.jumpElapsedMs);
        state.airborne = false;
        state.restoreFacingOnLand = false;
        return;
    }

    // Airborne preserves the velocity vector (DEC-036); intents cannot pre-empt the jump.
    // Observers play the takeoff arc (spline: parabolic segment; packets: extrapolated from the
    // takeoff packet), so mid-air updates stay silent — packets mode adds one corrective heartbeat.
    float const zOff = kJumpVelocity * t - 0.5f * kGravity * t * t;
    float const z = state.jumpStartZ + std::max(0.0f, zOff);
    if (sPlayerbotAIConfig.mlDuelMovementSplineTransport)
    {
        if (bot->movespline->Finalized())
        {
            SilentRelocate(bot, nx, ny, z, bot->GetOrientation());
            bot->m_movementInfo.fallTime = state.jumpElapsedMs;
        }
    }
    else if (!sPlayerbotAIConfig.mlDuelMovementThrottleBroadcast ||
             getMSTimeDiff(state.lastPacketMs, getMSTime()) >= 400)
        SendMovePacket(bot, state, MSG_MOVE_HEARTBEAT, state.moveFlags | MOVEMENTFLAG_FALLING, nx, ny, z,
                       bot->GetOrientation(), state.jumpElapsedMs, true, state.jumpDirWorld,
                       state.jumpSpeedXY);
    else
        SilentRelocate(bot, nx, ny, z, bot->GetOrientation());
}

void MlDuelMovement::StartJumpTurn(Player* bot, MlBotMovementState& state, float newFacing)
{
    if (sPlayerbotAIConfig.mlDuelMovementDisableJumpTurn)
        return;
    if (state.airborne || !state.moving || bot->IsRooted() || bot->HasUnitState(UNIT_STATE_ROOT))
        return;

    float const dir = bot->GetOrientation();
    if (sPlayerbotAIConfig.mlDuelMovementSplineTransport)
    {
        // One parabolic segment covers the whole deterministic arc (intents cannot pre-empt the
        // jump); its facing-angle is the post-flip bearing, so observers see the jump-turn at
        // takeoff. Facing is restored on the next ground segment after landing.
        if (!ApplyServerMoveState(bot, state.moveFlags | MOVEMENTFLAG_FALLING, bot->GetPositionX(),
                                  bot->GetPositionY(), bot->GetPositionZ(), dir))
            return;
        float const landT = 2.0f * kJumpVelocity / kGravity;
        float const speed = bot->GetSpeed(MOVE_RUN);
        BroadcastSplineSegment(bot, state, bot->GetPositionX() + std::cos(dir) * speed * landT,
                               bot->GetPositionY() + std::sin(dir) * speed * landT, bot->GetPositionZ(),
                               newFacing, uint32(landT * 1000.0f), true, kGravity);
        state.wireMoving = true;
    }
    // Takeoff keeps the pre-flip facing; the caller flips orientation right after, and the next
    // airborne heartbeat carries it. Facing is restored at landing (atomic until landing).
    else if (!SendMovePacket(bot, state, MSG_MOVE_JUMP, state.moveFlags | MOVEMENTFLAG_FALLING,
                             bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), dir, 0, true,
                             dir, bot->GetSpeed(MOVE_RUN)))
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

    // Rooted units must never emit jump packets or movement-flag broadcasts: observers hold
    // the unit rooted, and contradictory movement wedges real clients (root-flag heartbeat
    // spam in the server log was this path). The legacy instant turn handles facing.
    if (bot->IsRooted() || bot->HasUnitState(UNIT_STATE_ROOT))
        return false;

    MlBotMovementState* state = GetState(bot->GetGUID(), false);
    if (!state)
        return false;

    if (state->airborne)
    {
        // Mid jump-turn: allow the flip; landing restores the run facing. Spline transport keeps
        // the wire silent mid-air — the takeoff segment already carries the post-flip facing.
        bot->SetOrientation(bot->GetAngle(target));
        if (!sPlayerbotAIConfig.mlDuelMovementSplineTransport)
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
        if (!sPlayerbotAIConfig.mlDuelMovementSplineTransport)
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
        bool const valid = std::fabs(pz - bz) <= kProbeHeightDelta && bot->IsWithinLOS(px, py, pz + 2.0f) &&
                           OnNavMesh(bot, px, py, pz);
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
    bool const wasAirborne = state.airborne;
    bool const rooted = bot->IsRooted() || bot->HasUnitState(UNIT_STATE_ROOT);
    state.moving = false;
    state.airborne = false;
    state.moveFlags = 0;
    if (wasMoving)
    {
        // Server truth first, packet second: legacy pathing (MoveTowardPartner, PatrolNearPark)
        // gates on isMoving(), so executor move flags must never outlive the executor — even when
        // the STOP packet cannot be dispatched (root, live spline).
        bot->m_movementInfo.RemoveMovementFlag(MOVEMENTFLAG_FORWARD | MOVEMENTFLAG_BACKWARD |
                                               MOVEMENTFLAG_STRAFE_LEFT | MOVEMENTFLAG_STRAFE_RIGHT);
    }
    // FALLING goes with them whenever the executor owns it, and whenever the bot is rooted —
    // even if this stop is not ours to make. ROOT beside a moving or falling flag is the pair
    // that wedges an observing client's movement integrator (DEC-047), and a rooted unit is not
    // falling in the client's model either.
    if (wasAirborne || rooted)
        bot->m_movementInfo.RemoveMovementFlag(MOVEMENTFLAG_FALLING);
    if (sPlayerbotAIConfig.mlDuelMovementSplineTransport)
    {
        // Broadcast-only, so never rejected: one anchoring stop-spline at server truth on the
        // transition to halted (root and death included), then silence.
        if (state.wireMoving)
            BroadcastSplineStop(bot, state);
        return;
    }
    if (!wasMoving)
        return;
    if (!bot->IsAlive() || rooted)
        return;  // dead / server-side root: flags are stripped, but a packet would be rejected
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

bool MlDuelMovement::ApplyServerMoveState(Player* bot, uint32 moveFlags, float x, float y, float z,
                                          float o)
{
    if (!bot->movespline->Finalized())
        return false;
    o = Position::NormalizeOrientation(o);
    // Mirror of HandleMoverRelocation for the packet transport: flags replaced wholesale,
    // position applied, movement info restamped — identical server state under both transports.
    bot->m_movementInfo.SetMovementFlags(moveFlags);
    bot->UpdatePosition(x, y, z, o);
    bot->m_movementInfo.pos.Relocate(x, y, z, o);
    bot->m_movementInfo.time = getMSTime();
    return true;
}

void MlDuelMovement::BroadcastSplineSegment(Player* bot, MlBotMovementState& state, float destX,
                                            float destY, float destZ, float facing, uint32 durationMs,
                                            bool parabolic, float verticalAccel)
{
    // Layout per Movement::PacketBuilder::WriteMonsterMove for a linear one-waypoint path.
    WorldPacket data(SMSG_MONSTER_MOVE, 64);
    data << bot->GetGUID().WriteAsPacked();
    data << uint8(0);
    data << float(bot->GetPositionX()) << float(bot->GetPositionY()) << float(bot->GetPositionZ());
    data << uint32(splineIdGen.fetch_add(1, std::memory_order_relaxed));
    data << uint8(kMonsterMoveFacingAngle);
    data << float(Position::NormalizeOrientation(facing));
    data << uint32(parabolic ? Movement::MoveSplineFlag::Parabolic : Movement::MoveSplineFlag::None);
    data << uint32(std::max<uint32>(durationMs, 1));
    if (parabolic)
    {
        data << float(verticalAccel);
        data << uint32(0);  // effect_start_time: the arc spans the whole segment
    }
    data << uint32(1);  // one waypoint: the segment destination
    data << float(destX) << float(destY) << float(destZ);
    bot->SendMessageToSet(&data, false);
    state.facing = Position::NormalizeOrientation(facing);
    state.lastPacketMs = getMSTime();
}

void MlDuelMovement::BroadcastSplineStop(Player* bot, MlBotMovementState& state)
{
    // Layout per Movement::PacketBuilder::WriteStopMovement.
    WorldPacket data(SMSG_MONSTER_MOVE, 32);
    data << bot->GetGUID().WriteAsPacked();
    data << uint8(0);
    data << float(bot->GetPositionX()) << float(bot->GetPositionY()) << float(bot->GetPositionZ());
    data << uint32(splineIdGen.fetch_add(1, std::memory_order_relaxed));
    data << uint8(kMonsterMoveStop);
    bot->SendMessageToSet(&data, false);
    state.wireMoving = false;
    state.lastPacketMs = getMSTime();
}
