# Handoff 2026-08-01: DEC-031..034 win-anchored exploration round

Session goal (user directive on [#19](https://github.com/gamesh411/mod-playerbots/issues/19)): lean much more heavily on exploration and win-anchored training; treat on-next-swing spells with toggle care; investigate the missing Water Elemental / Freeze usage.

## What landed (exp/duel-rl-curriculum @ `40d14a06`)

- **DEC-031** on-next-swing masking while one is queued (`MlDuelSpellPool`).
- **DEC-032** pet paths resolve `GetGuardianPet()` + accept creature-template pet spells (the elemental is a Pet whose PetSpellMap never learns Freeze 33395); autocast pet spells excluded; Freeze added to the mage vocab as an untrained explorable slot.
- **DEC-033** exploration-first mixed farm (`StockSoftmaxTemperature` pins stock seats honest, model-less seats refuse spellbook-explore while pinned, out-of-vocab candidates floor to min in-vocab logit at tau>0); trainer gains `win-only`.
- **DEC-034** shapeshift-form spells out of the head (stance argmax collapse; no form feature).
- Dual-spec activation spells 63644/63645 and the DEC-030 auto-repeat ids masked **by explicit id** - 3018 passes the attr filter on this core, and any dropped label leaves an untrained vocab slot whose logits can win live argmax (measured: 1.5k tau=0 picks).
- Orchestrator: explore/gate split in `duel-farm` mixed profile; explore CSVs `ml_decisions_duel_s2_expl_{arms,frost}.csv` (~76k / ~145k rows, ranker seat tau=2.5 vs honest stock).

## Heads deployed

| Head | Scheme | Data | Offline argmax |
|------|--------|------|----------------|
| `warrior.pbml` (= dec033c) | win-only + inv balance | expl_arms (5,972 win rows) | 30 distinct: Charge 37%, Intercept 16%, Pummel 11%, Disarm, MS |
| `mage.pbml` (= dec033b) | win-else-expert + sqrt | expl_frost (64,617 rows, 5% won) | 12 distinct: Nova 42%, CS 29%, FB 16%, Ice Block, Deep Freeze |

## Smoke results (tau=0, honest stock, fresh CSVs, ~0.5-0.9k matches/seat)

| Seat | WR | vs stock 74.6/25.8 | vs S1 70.7/29.9 | Stacked |
|------|-----|--------------------|------------------|---------|
| Arms (S2 W) | **33.8%** (was 1.9%) | -40.7pp FAIL | -36.9pp FAIL | FAIL |
| Frost (S2 M) | **16.5%** (was 15.6%) | -9.3pp FAIL | -13.4pp FAIL | FAIL |

No freeze cut. Smoke CSVs archived as `*.dec033-smoke-20260801.csv` (plus `*.dec033-run1-3018leak-*` for the contaminated first arms run).

## Open items for the next round

1. **Iterate explore -> win-anchored retrain.** One round took Arms 1.9% -> 33.8%; next explore round starts from a competent head (tau=2.5 around dec033c), so win data quality and coverage should compound. Consider accumulating explore CSVs across rounds.
2. **Frost mage exploration hurts more than it helps** (explore WR 2.7% vs 16.5% argmax) - consider lower tau (~1.0-1.5) for the mage seat, or win-else-expert with the previous round's own argmax as teacher.
3. **Summon Water Elemental is still never picked at tau=0** (zero pet probes fired in the frost smoke). Only 59 summon examples existed in the win data. Levers: pet-state feature (map fog), summon-biased exploration, or scaffolding the summon like melee (DEC-030 family: "engagement scaffolding" - summon at duel start is arguably scripted-baseline behavior).
4. **Freeze end-to-end still unverified live** - the `DEC-032 CanCastPetSpell Freeze result=` probe never fired because no elemental was ever up on a spellbook seat. First tau>0 mage farm on the current binary will confirm; watch for `DEC-027 Freeze cast OK`.
5. Probes to remove once confirmed: `DEC-032 pet probe`, `DEC-032 CanCastPetSpell Freeze result` (both in module code).
6. Feature fog: pet-state bit and form/stance bit unlock summon timing and stance dancing (DEC-032/034).
