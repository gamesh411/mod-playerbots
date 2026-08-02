# Handoff 2026-08-02: DEC-033 round-2 smokes (arms 59.7%, frost 24.2%, no freeze)

Session goal: finish the second DEC-033 explore -> win-anchored iteration started on 2026-08-01.
The round-2 explore CSVs (`ml_decisions_duel_s2_expl_arms.csv` ~40MB @ tau=2.5, `ml_decisions_duel_s2_expl_frost.csv` ~70MB @ tau=1.5 per the round-1 lever) and the retrained `dec033r2` heads existed but were never smoked at tau=0.

## Heads under test (deployed canonical, `dec033r2`)

Trained by the prior session right after each explore phase finished (warrior 00:02, mage 00:17, deployed 00:18 on 2026-08-02); exact trainer flags were not logged, but the file timeline matches the DEC-033 round-1 schemes (warrior win-only + inv balance, mage win-else-expert + sqrt) rerun on the round-2 explore CSVs.
Warrior head is `out=47` (stances left the vocab per DEC-034); mage stays `out=220`.

Offline degeneracy gate (`tools/ml/check_s2_argmax.py`, 20k states from the round-2 explore CSVs):

- warrior: 30 distinct argmax - Charge 48.4%, Pummel 19.2%, Intercept 7.0%, Retaliation 6.5%, MS 5.0%; expert-agreement 2.3% (expected for win-anchored self-imitation).
- mage: 12 distinct argmax - Frost Nova 44.5%, Counterspell 31.5%, Frostbolt 16.4%, Ice Block 3.4%; expert-agreement 40.3%.

## Smoke setup

- Orchestrator back to gate mode (`DuelFarmMixedSeatSoftmaxTemperature = 0.0`, wotlk-playerbots-server `1d641ca`); seats via `-DuelMixedSeat arms-ranker` / `frost-ranker`.
- Applied conf verified per seat: ranker class on the S2 pbml, other class empty-path Softmax-stock with `StockSoftmaxTemperature = 0.0` (honest stock, DEC-033 instrument fix).
- Stock seats confirmed honest in the data: mage frostbolt 42% + full kit; warrior slam 30% / heroic strike 15% / charge / MS.

## Results (tau=0, honest stock, delta=0.02, fresh CSVs)

| Seat | WR | vs stock 74.6/25.8 | vs S1 70.7/29.9 | Stacked |
|------|-----|--------------------|------------------|---------|
| Arms (S2 W) | **59.7%** (1347-911, 2258) | -14.9pp FAIL | -11.1pp FAIL | FAIL |
| Frost (S2 M) | **24.2%** (816-2560, 3376) | **-1.6pp** FAIL | -5.7pp FAIL | FAIL |

Round trend (tau=0 vs honest stock): Arms 1.9% -> 33.8% -> **59.7%**; Frost 15.6% -> 16.5% -> **24.2%**.
No freeze cut.
Smoke CSVs archived as `ml_decisions_duel_mixed_{arms,frost}_ranker.dec033r2-smoke-20260802.csv`.

## Live behavior notes

- Arms live mix is a real kit now: Slam 14.6%, melee engaged 10.1%, Cleave 8.2%, Heroic Strike 7.6%, Battle Shout, Charge, Berserker Rage, Thunder Clap - no single-spell collapse.
- **Freeze 33395 fired live at tau=0 for the first time**: 171 rows in the frost smoke (post `672262ea` glyphed-elemental SQL fix).
- Summon Water Elemental 31687 is still picked 0 times at tau=0. The Freeze casts happen because the Glyph of Eternal Water elemental is permanent and the scripted out-of-duel AI summons it between duels, so it is already up when the duel starts. In-head summon timing remains unlearnable without a pet-state feature (map fog).

## Open items for round 3

1. **Iterate again - the compounding holds.** Arms gained ~+26pp both rounds; frost's lower tau (1.5) more than tripled its round-1 gain (+7.7pp vs +0.9pp). Next explore round starts from a 59.7% arms head, so win-row quality keeps improving.
2. **Frost is 3.6pp from the stock gate** (24.2% vs needed 27.8%). One more round plausibly clears stock; the S1 gate (31.9%) likely needs the pet-state feature or summon scaffolding.
3. **Arms gap is bigger** (needs 76.6% for stock +2pp). Consider accumulating explore CSVs across rounds (round-1 open item, still unadopted) and/or a longer arms explore phase.
4. Log the trainer invocation next time a head is rebuilt - this round's flags had to be reconstructed from file timestamps.
5. Fog unchanged: pet-state + form/stance features (DEC-032/034), summon scaffolding vs learning (DEC-030 family).
