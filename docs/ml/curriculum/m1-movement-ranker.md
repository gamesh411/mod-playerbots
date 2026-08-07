# M1 - Learned movement-intent ranker (9-way head)

Stage freeze per DEC-019 / DEC-039 / DEC-041.
First learned movement stage of the movement extension arc (M0 -> M1 -> M2), eval epoch 2.

## What this stage is

Per-class movement heads (90-D state -> 9 foe-relative intent logits) drive the DEC-036 packet executor's intent channel, replacing the scripted Arms-chase / Frost-kite pick.
The executor's physics (100 ms subticks, probes, strafe-first facing, snare-window sprint mechanics) is unchanged; only the intent decision is learned.
The ability channel stays pinned to the S0 Softmax-stock sentinel throughout farm and eval.

## Freeze summary

| Item | Value |
|------|-------|
| Git tag | `stage/m1-movement-ranker` |
| Canonical heads | `artifacts/duel/m1/warrior.pbml` (= r2-dagger snapshot), `artifacts/duel/m1/mage.pbml` (= r3-eo snapshot) |
| Model shape | PBML1, input 90, hidden 64, 9 logits, vocab 0-8 (`MlMovementIntent` order) |
| Conf (farm) | `duel-farm` + `MlDuelMovement.Policy=ranker`, `SoftmaxTemperature=10`, per-class `MlModelPathDuelMovement.*` |
| Conf (demo) | `-ServerProfile duel-m1` (DEC-040 mechanics: tau=0 argmax, 10 bots, full-fidelity broadcasts) |
| Data tag | `ml_movement_duel_v1` (dedicated movement CSV, 500 ms intent horizon + intent changes) |
| Frozen at | 2026-08-07 |

## Freeze gate (DEC-039; delta = 0.02, >= 2.4k matches/run, movement tau=0, abilities stock tau=0)

Fresh epoch-2 M0<->M0 baseline: warrior 39.3% / mage 61.5%.

| Seat | Head | WR | Verdict |
|------|------|-----|---------|
| Mage | r3-eo (win-else-expert) | **63.8%** | **PASS** (+2.3pp >= +2.0pp) |
| Warrior | r3-eo | 36.9% | fail (-2.4pp) |
| Warrior | r3-eo reward-weighted | 32.1% | fail (escalation lever hurt) |
| Warrior | r2-dagger clone (frozen) | 40.3% | parity waiver (+1.0pp, ~1 sigma) - DEC-041 |

## Movement-quality metrics (gate runs, argmax)

| Metric | M0 baseline | M1 frozen |
|--------|-------------|-----------|
| Mage cast-band uptime (15-30y, LoS) | 16.3% | **19.0%** |
| Mage mean foe distance | 9.7 y | 11.5 y |
| Warrior melee uptime | 44.5% | 41.0% |
| Warrior mean foe distance | 6.8 y | 7.8 y |
| Mage teacher-disagreement (argmax) | - | 33.3% |
| Warrior teacher-disagreement (argmax) | - | 9.5% |

The mage head's WR uplift is corroborated by range control: it holds the cast band more and kites wider than the scripted teacher.
The warrior clone deviates from the teacher on ~10% of states at parity outcomes.

## Training recipe (DEC-039, executed in full)

1. **Round B bootstrap BC**: behavior-clone the frozen M0 movers on 1.17M/1.05M dedicated movement rows (4 fresh-park farm windows, post navmesh/flag fixes). Holdout teacher agreement 97.7%/98.2%; band structure reproduced.
2. **DAgger x2**: ranker deployed at tau=10 both seats, scripted teacher logged every subtick as `expert_movement_intent`; >= 1M fresh rows/class/round; full-aggregate CE retrain each round (never drop rows).
3. **Expert-off**: one `win-else-expert` round (own intent on won episodes, else teacher) + sqrt label balance on the full ~4.6M/4.1M-row aggregate.
4. Every deploy passed the offline degeneracy gate (state-conditional argmax; bootstrap additionally >= 90% holdout teacher agreement + band structure; matches with `match_id % 10 == 7` held out from all training).

Trainer: `tools/ml/train_movement_ranker.py`; gate: `tools/ml/check_movement_argmax.py`.

## Deviations and events during execute

- **Navmesh-validated probes and steps** (`0508663c`): candidate points must have a Detour poly at height - kiters could previously stair-climb onto unreachable props (tree trunks, ballista). Landed mid-round-B; pre-fix data discarded from training.
- **Move-flag hygiene** (`0508663c`): duel-end flag strip runs for dead bots; the bracket self-heals wedged directional flags. Fixed the farm pool draining to ~26 bots over 2h (patrol/pairing gated on a stale `isMoving()`).
- **Rooted-unit packet guards** (`faaf3687`): no jump packets or movement-flag broadcasts from rooted bots (root-state contradiction on the wire).
- **Teacher label semantics**: `expert_movement_intent` is the raw scripted pick each subtick (pre-debounce), diverging from M0's post-debounce logging.
- **Farm operation**: 45-minute fresh-park cycles (pool drain mitigation), CSV rotated per window; per-file episode keys in the trainer.
- **Client freeze near the farm** under executor packet movement: investigated (create blocks and relayed stream verified well-formed), split out to [#27](https://github.com/gamesh411/mod-playerbots/issues/27). Demo-scale profiles are expected unaffected (~5 movers).
- **Warrior parity waiver**: see DEC-041 - no learned head beat the near-optimal scripted chase; the certified clone ships instead.

## Reproduce

```
# demo (argmax, 10 bots)
.\scripts\ensure_running.ps1 -ProfileGB 32 -ServerProfile duel-m1 -RestartServers

# farm state at freeze
.\scripts\ensure_running.ps1 -ProfileGB 32 -ServerProfile duel-farm -RestartServers
# (duel-farm knobs at freeze: Policy=ranker, tau=10, canonical m1 heads)
```

Binary: module tag `stage/m1-movement-ranker` on `exp/duel-rl-curriculum`; core Playerbot branch with `d5b4e6346` (movement-validation bypass).
