# Handoff — S2 execute progress + first freeze fail (2026-07-29)

For a new agent session. Read after [S1 soft-fail handoff](handoff-2026-07-28-s1-softfail-s2.md) and **DEC-026**.

## Where we are

- **Map:** [#4](https://github.com/gamesh411/mod-playerbots/issues/4)
- **Ticket:** [#19](https://github.com/gamesh411/mod-playerbots/issues/19) — S2 execute **in progress**; freeze **not** cut
- **Branch:** `exp/duel-rl-curriculum` @ ~`cdfd75af` (+ docs commit after this handoff)
- **Orchestrator:** `wotlk-playerbots-server` `a23dd38` (`duel-farm` → spellbook / S2 learners / S1 teachers / `s2_eo` CSV)

## What landed (runtime)

| Piece | Location / note |
|-------|-----------------|
| Multi-logit PBML | `MlMlpModel` `output_dim>1` + `vocab`; S1 scalar path preserved |
| Spellbook pool | `MlDuelSpellPool` — no highest-rank collapse; pet spells included |
| Engine | `ActionPolicy=ranker` + `SpellPool=spellbook` Softmax; DAgger expert from S1 teacher τ=0 on queue∩legal |
| Pet Freeze (DEC-027) | `CanCastPetSpell` / `CommandPetCastSpell`; spell **33395** startup dump + cast OK/FAIL logs. **In-farm Freeze action count stayed ~0** — do not claim pet-root combos yet |
| Accept-duel fix | `cdfd75af` — duel ML only when `DUEL_STATE_IN_PROGRESS` (CHALLENGED no longer starves `accept duel`) |
| Train | `tools/ml/train_spellbook_ranker.py` |
| Eval | `eval_duel_winrate.py --baseline-csv-s1` stacked gate |

## Train / farm timeline

| Phase | CSV (under `C:\AzerothCore-server\`) | Train |
|-------|--------------------------------------|--------|
| Bootstrap | `ml_decisions_duel_s2.csv` (~78 MB) | uniform Softmax explore |
| DAgger r1 | `…_s2_r2.csv` (~107 MB) | CE + `--imitate-expert` |
| DAgger r2 | `…_s2_r3.csv` (~110 MB) | CE + imitate; frozen vocab **49W / 220M** |
| Expert-off | `…_s2_eo.csv` (~95 MB+; ~409k rows / ~3.1k matches at retrain) | aggregate CE **without** imitate |

Frozen vocab files: `artifacts/duel/s2/vocab.{warrior,mage}.txt`.

Canonical working PBMLs (expert-off, **not** freeze-cut):

- `artifacts/duel/s2/warrior.pbml` — `in=70`, `out=49`, vocab=49
- `artifacts/duel/s2/mage.pbml` — `in=70`, `out=220`, vocab=220

Local pre-* backups (untracked / optional): `*.pre-r2-*`, `*.pre-r3-*`, `*.pre-eo-*`.

## First stacked freeze eval (FAIL — catastrophic)

δ=**0.02**, τ=**0**, ~2.6–2.8k matches/seat. Baselines: stock v3 W **74.6%** / M **25.8%**; S1 farm v4 W **70.7%** / M **29.9%**.

| Seat | S2 ranker WR | vs stock | vs S1 | Stacked |
|------|--------------|----------|-------|---------|
| Arms (S2 W vs “stock” M) | **0.0%** (0–2626) | −74.6pp FAIL | −70.7pp FAIL | **FAIL** |
| Frost (S2 M vs “stock” W) | **0.1%** (3–2807) | −25.7pp FAIL | −29.8pp FAIL | **FAIL** |

Mixed CSVs (S2 run; S1 mixed archived as `*.s1-archive.csv`):

- `ml_decisions_duel_mixed_arms_ranker.csv`
- `ml_decisions_duel_mixed_frost_ranker.csv`

**Do not cut `stage/s2`.** This is not a close miss.

## Suspects (next session — diagnose before more farm)

1. **Mixed-seat stock path dishonest:** with `SpellPool=spellbook` + `ActionPolicy=ranker`, empty PBML still enters spellbook Softmax (uniform logits → τ=0 → always candidate `[0]`), **not** Softmax-stock over the queue. Fix: stock seat must use queue Softmax-stock when `!HasModelFor(class)`.
2. **S2 policy quality:** expert-off was CE on Softmax-explored actions; farm heavily favored junk (e.g. Auto Attack **6603**). May need win-filter / keep DAgger pressure longer.
3. **Cast path:** sample mixed CSVs for top spell ids and Execute fallthrough before retrain.

Proposed order: fix (1) → sample casts (2) → short mixed smoke (~500 matches) → only then full gate.

## Sparring note

Real-player challenge: pending duel set `player->duel` in `CHALLENGED`; old code treated that as in-duel and starved accept. Fixed in `cdfd75af`. Re-test sparring after that binary is installed.

## Pointers

- Stage card: [`docs/ml/curriculum/s2-spellbook-ranker.md`](../curriculum/s2-spellbook-ranker.md)
- Artifacts: [`artifacts/duel/s2/README.md`](../../../artifacts/duel/s2/README.md)
- Design: **DEC-026** in `docs/ml/DECISIONS.md`
- Orchestrator knobs: `wotlk-playerbots-server/scripts/config.ps1` (`DuelFarm*`, `-DuelMixedSeat`)
