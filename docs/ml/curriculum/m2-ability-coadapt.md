# M2 - Ability-head co-adaptation on the movement-active world

Stage freeze per DEC-019 / DEC-042 / DEC-051.
Final stage of the movement extension arc (M0 -> M1 -> M2), eval epoch 2.

## What this stage is

Per-class spellbook multi-logit ability heads (112-D duel_v6 state -> per-spell-id logits over the class@80 legal spellbook) retrained fresh on movement-active farm data, with the frozen M1 movement heads driving both seats.
This is the first ability stage trained on clean teacher labels (DEC-049) and the first certified in the DEC-044 world (fair-rematch pet reset, 25% unglyphed farm mages).
The feature vector adds CF_PET 90-99 and CF_FORM 100-111 (DEC-043); warrior stances re-entered the vocab with same-form masking.

## Freeze summary

| Item | Value |
|------|-------|
| Git tag | `stage/m2-ability-coadapt` |
| Canonical heads | `artifacts/duel/m2/warrior.pbml` (= r1-win snapshot), `artifacts/duel/m2/mage.pbml` (= r0-bc snapshot) |
| Model shape | PBML1, input 112, hidden 256; 45 logits (warrior) / 218 logits (mage), frozen r0 vocab (`*.r0.vocab.txt`) |
| Movement | Frozen M1 canonical heads both seats (`stage/m1-movement-ranker`), transport `packets` (DEC-048) |
| Conf (farm) | `duel-farm`: `ActionPolicy=ranker`, `SpellPool=spellbook`, tau=10, `PetReset=1`, `UnglyphedMageShare=0.25` |
| Conf (demo) | `-ServerProfile duel-m2` (DEC-040 mechanics + certified-era world: PetReset on, unglyphed share 0.25) |
| Data tag | `duel_v6` (135-col CSV, 112-D features, dedicated per-round logs) |
| Frozen at | 2026-08-18 |

## Freeze gate (DEC-042; delta = 0.02, >= 2.4k matches/run, tau=0 both channels, DEC-037 rules)

Fresh S0-sentinel+M1 baseline: warrior 45.9% / mage 54.6% (10.7k matches per seat).
Each seat's full round trajectory was measured before freezing (DEC-051); >= 3.2k matches per leg; runner cross-validated by reproducing the round-2 frost leg (35.4% vs 35.8%).

| Seat | Head | WR | Verdict |
|------|------|-----|---------|
| Warrior | **r1-win (frozen)** | **58.0%** | **PASS** (+12.1pp >= +2.0pp) |
| Warrior | r0-BC | 33.0% | fail (-12.9pp) |
| Warrior | r2-win | 46.9% | fail (+1.0pp, parity) |
| Mage | **r0-BC (frozen)** | **75.2%** | **PASS** (+20.6pp >= +2.0pp) |
| Mage | r1-win | 49.7% | fail (-4.8pp) |
| Mage | r2-win | 35.8% | fail (-18.8pp) |

Both frozen heads also hold their per-deploy gates on record: state-conditional argmax (warrior 18/45 distinct, mage 20/218) and the DEC-050 pet-down floor (mage r0-BC: summon argmax at some point in 57.7% of held-out pet-down duel-seats, floor 25%).

## Showcase rows (report-only, DEC-042)

Statue-era S2 heads (dec033r3) measured on the movement-active world against the same fresh S0+M1 baseline.
The +M0 rows run scripted movement both seats and therefore carry a baseline-mismatch caveat (the baseline world moves with M1 heads).

| Combo | Warrior WR | Mage WR |
|-------|------------|---------|
| dec033r3 + M1 movement | 44.6% | 38.8% |
| dec033r3 + M0 scripted movement | 50.2% | 37.6% |

The statue-era heads sit at stock-or-worse on the movement-active world on three of four rows; the frozen M2 pair beats them on both seats by 8-37pp.

## Training recipe (DEC-042, executed; loop closed by DEC-051)

1. **Round 0 BC bootstrap**: behaviour-clone `expert_action` (stock relevance argmax intersected with the castable set - DEC-049 label semantics) on 4.01M duel_v6 rows; capacity sweep picked hidden 256 (warrior CE 1.058 -> 1.031 vs 128); `--label-scheme expert --balance-labels sqrt --epochs 40`.
2. **Round 1 win-only**: `--label-scheme win-only --balance-labels sqrt`, frozen r0 vocab, on 4.02M rows farmed by the r0 heads at tau=10 (warrior 980k / mage 443k labelled rows).
3. **Round 2 win-only**: same recipe on 5.90M rows farmed by the r1 heads (warrior 1.69M / mage 428k labelled rows).
4. Holdout `match_id % 10 == 7` never trained; every deploy passed the offline degeneracy gate + DEC-050 floor.

The loop was closed at the per-seat peaks (DEC-051): the warrior peaked at round 1 (+25pp over its BC clone, trained on wins earned against the strong r0-BC mage), the mage at round 0 (the projected BC clone already beats stock by 20.6pp).
Win-only label quality tracks opponent strength; round 2 regressed both seats and the offline degeneracy gate cannot see policy-strength regressions - from M2 on, every deploy requires a tau=0 WR leg (DEC-051 deploy discipline).

Trainer: `tools/ml/train_spellbook_ranker.py`; gates: `tools/ml/check_s2_argmax.py`, `tools/ml/eval_duel_winrate.py`.

## Deviations and events during execute

- **DEC-049 teacher-label fix**: argmax-then-project labelled only ~14%/44% of rows and substituted the bot's own explore pick for missing labels (87%/73% fake expert==action agreement); fixed to intersect-first + explicit-missing. Summon Water Elemental was also barred from the expert search by a name heuristic - the exact action the DEC-044 floor guards. S2's DAgger rounds trained on the same contaminated shape (does not reopen S2, DEC-035).
- **DEC-050 floor restatement**: the per-tick pet-down floor was unreachable by any correct policy (once-per-duel action, 26.8 ready ticks per duel); restated per duel-seat at >= 25%.
- **Upstream sync mid-execute**: core + modules rebased onto upstream between the round-2 farm and its gate (`post-upstream-sync-20260816`); sync verified innocent of the gate result (M1 baseline reproduced from archived CSV; clone-head teacher agreement 0.9828 post-sync).
- **Round-2 regression on both seats** went undetected at deploy time because only offline gates ran; quantified and turned into the DEC-051 deploy discipline.
- **OS sleep during the round-2 farm** (6.5 h gap, zero in-gap duels, win labels per-duel so unaffected); farm nights disable AC sleep first.

## Reproduce

```
# demo (argmax, 10 bots, certified-era world)
.\scripts\ensure_running.ps1 -ProfileGB 32 -ServerProfile duel-m2 -RestartServers

# farm state at freeze (round-3 recipe retired; heads canonical)
.\scripts\ensure_running.ps1 -ProfileGB 32 -ServerProfile duel-farm -RestartServers
```

Binary: module tag `stage/m2-ability-coadapt` on `exp/duel-rl-curriculum`; core Playerbot branch at `97c36e46b` (DEC-047 ROOT-flag fix included).
