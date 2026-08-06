# Stage M0 — Movement substrate (packet executor + scripted intent movers)

> Design locked in **DEC-036** / [#20](https://github.com/gamesh411/mod-playerbots/issues/20). Executor + movers landed via [#21](https://github.com/gamesh411/mod-playerbots/issues/21). Code+conf sentinel (no PBML). Landing M0 in the farm opens **eval epoch 2**: S-track evals stay comparable only intra-epoch.

| Field | Value |
|-------|--------|
| **Policy** | Ability head: Softmax-stock over the scripted queue (S0 sentinel, DEC-022). Movement: scripted intent policies through the packet executor (DEC-036) |
| **Vocab** | Movement: 9-way foe-bearing-relative intent (hold + 8 compass points), ~0.5 s horizon, 100 ms subtick |
| **Movement** | Direct velocity control via synthesized client movement packets through the bot `WorldSession`; slide-along-obstacle probe clamps; strafe-first facing solver (70° cast-arc limit, atomic jump-turn); Arms chase / Frost 15–30y kite with snare-window sprint. Legacy scripted movers masked in duels while enabled |
| **Features / schema** | CF_MOVE 70–89 (kinematics 8, impairment 4, probes 8); movement head input 90-D; `duel_v5` CSV adds `realized_heading`, `movement_intent`, `expert_movement_intent`; ability head stays 82-D |
| **Conf** | `AiPlayerbot.MlDuelMovement.{Enable=0, SubtickMs=100, ProbeRangeYd=4, ThrottleBroadcast=0}`; orchestrator `duel-farm` profile flips `Enable=1` and `ThrottleBroadcast=1` |
| **Throughput gate** | **84.2 % — waived (DEC-038)**: movement 1,277 vs control 1,516 matches / 20 min (3,831 vs 4,548 duels/hour), fresh-park protocol, DEC-037 rules; movement duels 32 % shorter, server ~16 % of one core |
| **Policy artifact** | code+conf sentinel (no PBML) |
| **Conf profile** | `duel-farm` + `MlDuelMovement.Enable=1`; stage replay `duel-m0` (**DEC-040**) |
| **Data tag** | `ml_decisions_duel_v5+90d` |
| **Git tag** | `stage/m0-packet-executor` |
| **Manifest path** | `artifacts/duel/m0/manifest.json` |
| **Status** | **frozen** (2026-08-06, DEC-038 gate waiver) |

## Goal

Replace legacy scripted movement in duels with a client-authentic movement channel (front-arc speed model, strafe-kiting, jump-turns) so M1 can learn a 9-way movement-intent ranker on the same executor the scripted M0 movers drive — with the movers doubling as the DAgger teacher.

## Deviations from DEC-036

- Jump-turn flips facing at takeoff (not apex): the cast fires synchronously when the facing request arrives, so the flip cannot be deferred mid-air. Facing is still restored at landing and the jump stays atomic.
- A duel-flag leash (35 y from the arbiter) clamps retreat intents so kiting can never forfeit the duel out-of-bounds.
- Broadcast throttling is a conf flag (`ThrottleBroadcast`, default off): full-fidelity 10 Hz packet broadcasts for demos, real-client cadence (state changes + 500 ms heartbeats, silent server-side integration between) for farms.
- Gate remediation (first pass failed at 86 %): probes refresh 4 of 8 directions per pass (alternate halves, each slot ≤ 2 subticks stale) and are skipped entirely while holding.
- DEC-037 landed mid-execute (fair rematch: rage/RP zeroed + all cooldowns cleared at duel end); both gate runs are measured under DEC-037 rules.
