# Handoff 2026-08-02: DEC-033 round 3 - both seats plateau (arms 58.6%, frost 24.7%, no freeze)

Session goal: run the third DEC-033 explore -> win-anchored retrain -> tau=0 smoke iteration, with the round-2 levers applied (longer arms explore, frost tau kept at 1.5, explore CSVs accumulated across rounds).
Headline: the round-over-round compounding stalled - Arms 59.7% -> **58.6%** and Frost 24.2% -> **24.7%**, both inside noise of round 2 and both still stacked FAIL.
No freeze cut.

## Explore phases (accumulated CSVs)

The explore CSVs accumulate across rounds because the logger appends and the files are never rotated; the round-2 heads were already trained on rounds 1+2, so the round-1 "consider accumulating" lever was de facto adopted then.
This round appended on top of that:

- Arms: `ml_decisions_duel_s2_expl_arms.csv` 164,666 -> **370,943 rows** (+206k @ tau=2.5, vs +88k in round 2 - the "longer arms explore" lever).
- Frost: `ml_decisions_duel_s2_expl_frost.csv` 292,703 -> **446,220 rows** (+153k @ tau=1.5).
- Explore-seat behavior sampled early in each phase: warrior spread (no Cleave collapse), stock opponents honest.
- The frost explore phase picked **Summon Water Elemental 31687** 10 times and cast **Freeze 33395** 38 times in its first ~19k rows - win examples for the pet line now exist, but stay thin.

## Retrains (flags logged verbatim this round - round-2 open item 4)

```
python tools/ml/train_spellbook_ranker.py \
  --csv C:/AzerothCore-server/ml_decisions_duel_s2_expl_arms.csv \
  --out artifacts/duel/s2/warrior.dec033r3.pbml \
  --self-class warrior --duel-only --drop-duel-noise \
  --label-scheme win-only --balance-labels inv \
  --drop-labels 6603 75 2764 3018 5019 63644 63645 2457 2458 71 \
  --hidden 128 --epochs 60

python tools/ml/train_spellbook_ranker.py \
  --csv C:/AzerothCore-server/ml_decisions_duel_s2_expl_frost.csv \
  --out artifacts/duel/s2/mage.dec033r3.pbml \
  --self-class mage --duel-only --drop-duel-noise \
  --label-scheme win-else-expert --balance-labels sqrt \
  --drop-labels 6603 75 2764 3018 5019 63644 63645 \
  --hidden 128 --epochs 60
```

The drop-label sets mirror `MlDuelSpellPool`'s static masks (DEC-030 toggles + dual-spec ids, warrior stances per DEC-034).

- Warrior: rows=61,208 win-only (10x round 1's 5,972; won_frac 0.412), vocab=47, CE 3.66 -> 3.28, out=47.
- Mage: rows=184,367 (win_self 20,869, won_frac 0.113), vocab=220, CE 5.07 -> 4.88, out=220.

## Offline degeneracy gate (`check_s2_argmax.py`, 20k states, excludes = pool masks)

- Warrior: **33 distinct argmax** - Intercept 30.3%, Disarm 15.3%, Retaliation 12.6%, Whirlwind 11.8%, Execute 6.9%, Charge 6.5%, Pummel 5.2%; expert-agreement 2.4% (expected for win-only).
- Mage: **30 distinct argmax** (round 2: 12) - Frost Nova 37.5%, Counterspell 22.6%, Frostbolt 13.9%, Ice Block 7.8%, Deep Freeze 3.8%, **Freeze 33395 0.9%** (first time in argmax); expert-agreement 44.9%.

Both passed; deployed over `artifacts/duel/s2/{warrior,mage}.pbml` (previous heads kept as `*.pre-dec033r3-20260802.pbml`).

## Smoke results (tau=0, honest stock, delta=0.02, fresh CSVs)

| Seat | WR | vs stock 74.6/25.8 | vs S1 70.7/29.9 | Stacked |
|------|-----|--------------------|------------------|---------|
| Arms (S2 W) | **58.6%** (1224-865, 2089) | -16.0pp FAIL | -12.1pp FAIL | FAIL |
| Frost (S2 M) | **24.7%** (662-2014, 2676) | **-1.1pp** FAIL | -5.2pp FAIL | FAIL |

Round trend (tau=0 vs honest stock): Arms 1.9 -> 33.8 -> 59.7 -> **58.6**; Frost 15.6 -> 16.5 -> 24.2 -> **24.7**.
Smoke CSVs archived as `ml_decisions_duel_mixed_{arms,frost}_ranker.dec033r3-smoke-20260802.csv`.
Stock seats verified honest in both runs (mage frostbolt-led with full kit; warrior slam/heroic strike/charge).

## Live behavior notes

- Arms live mix is a real kit (Heroic Strike / Cleave / Slam / Thunder Clap / Charge leading), no collapse; Sunder Armor r1 (7386) shows up as a ~2.5k-row filler pick - low-value but legal, a residue of label noise rather than a sink.
- Frost live mix: Frostbolt-led with Counterspell / Frost Nova / Ice Barrier; **Freeze fired 256 rows** at tau=0.
- **Summon 31687: 0 picks at tau=0**, unchanged - Freeze only fires because the glyphed permanent elemental is summoned by scripted out-of-duel AI between duels.

## Interpretation: the loop has plateaued

Round 3 more than doubled the accumulated explore data, multiplied the warrior's win-row count by 10 vs round 1, and produced the healthiest offline argmax spreads yet - and the tau=0 winrates did not move.
The win-anchored CE scheme on the current 70-D features appears converged; the remaining gap (arms -16pp, frost -1.1pp to the stock gate) is no longer a data-volume or label-scheme problem.
The blockers the map already carried as fog are now the binding constraints:

1. **Feature starvation** (DEC-032/034 fog): no pet-state bit, no form/stance bit, and doubtful separability of chase states ("at range, Intercept ready") - the head cannot represent the states whose actions decide the remaining duels.
2. **Summon Water Elemental** (DEC-030 family fog): unlearnable at tau=0 without a pet feature and/or scaffolding; frost's S1 gap (-5.2pp) plausibly lives here.

Both fog patches were graduated to tickets this session (see #19 comment / map).

## Open items

1. Do not run another explore/retrain round on the current feature vector expecting gate progress - the loop is converged; resolve the feature-extension and summon tickets first.
2. Frost sits 3.1pp under the stock gate and 5.2pp under S1; arms needs +18pp to clear stock +delta - arms likely also needs the form/stance feature (Overpower/stance lines) or movement-adjacent features beyond this map's scope.
3. Trainer invocations are now logged here (open item 4 closed).
4. Fog/probe status: DEC-032 probes removed (`9b5bdc42`); Freeze verified live at tau=0 both this round (256 rows) and last.
