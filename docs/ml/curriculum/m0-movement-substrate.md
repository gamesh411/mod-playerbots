# Stage M0 — Movement substrate (packet executor + scripted intent movers)

> Design locked in **DEC-036** / [#20](https://github.com/gamesh411/mod-playerbots/issues/20). Executor + movers landed via [#21](https://github.com/gamesh411/mod-playerbots/issues/21). Code+conf sentinel (no PBML). Landing M0 in the farm opens **eval epoch 2**: S-track evals stay comparable only intra-epoch.

| Field | Value |
|-------|--------|
| **Policy** | Ability head: Softmax-stock over the scripted queue (S0 sentinel, DEC-022). Movement: scripted intent policies through the packet executor (DEC-036) |
| **Vocab** | Movement: 9-way foe-bearing-relative intent (hold + 8 compass points), ~0.5 s horizon, 100 ms subtick |
| **Movement** | Direct velocity control via synthesized client movement packets through the bot `WorldSession`; slide-along-obstacle probe clamps; strafe-first facing solver (70° cast-arc limit, atomic jump-turn); Arms chase / Frost 15–30y kite with snare-window sprint. Legacy scripted movers masked in duels while enabled |
| **Features / schema** | CF_MOVE 70–89 (kinematics 8, impairment 4, probes 8); movement head input 90-D; `duel_v5` CSV adds `realized_heading`, `movement_intent`, `expert_movement_intent`; ability head stays 82-D |
| **Conf** | `AiPlayerbot.MlDuelMovement.{Enable=0, SubtickMs=100, ProbeRangeYd=4}`; orchestrator `duel-farm` profile flips `Enable=1` |
| **Throughput gate** | duels/hour ≥ 90 % of the same-config control run (`Enable=0`) — _result recorded at freeze_ |
| **Policy artifact** | code+conf sentinel (no PBML) |
| **Conf profile** | `duel-farm` + `MlDuelMovement.Enable=1` (stage-replay `duel-m0` pending #13) |
| **Data tag** | `ml_decisions_duel_v5+90d` |
| **Git tag** | _TBD `stage/m0-…` (cut at freeze)_ |
| **Manifest path** | `artifacts/duel/m0/manifest.json` |
| **Status** | not frozen |

## Goal

Replace legacy scripted movement in duels with a client-authentic movement channel (front-arc speed model, strafe-kiting, jump-turns) so M1 can learn a 9-way movement-intent ranker on the same executor the scripted M0 movers drive — with the movers doubling as the DAgger teacher.

## Deviations from DEC-036

- Jump-turn flips facing at takeoff (not apex): the cast fires synchronously when the facing request arrives, so the flip cannot be deferred mid-air. Facing is still restored at landing and the jump stays atomic.
- A duel-flag leash (35 y from the arbiter) clamps retreat intents so kiting can never forfeit the duel out-of-bounds.
