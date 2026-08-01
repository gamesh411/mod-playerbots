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

## Update 2026-08-01 — suspects (1) and (2) confirmed, (1) fixed

Suspect (1) confirmed in code and data: the spellbook block had no model check, so at τ=0 a model-less "stock" seat argmaxed uniform logits into `candidates[0]` junk spam (mages: Frostfire Bolt r1 **44614**; warriors: Battle Stance **2457** / Sunder r1 **7386**).
Fixed in `356c191b`: spellbook ranker requires `HasMultiLogitFor(class)`; uniform explore only at τ>0 (bootstrap unchanged); model-less seats fall to the DEC-025 queue Softmax-stock fallback.
Suspect (2) also confirmed: at τ=0 the expert-off arms policy is ~pure Cleave r8 (**47520**, on-next-melee, never lands without auto attack) and frost is ~pure Auto Attack (**6603**).
Post-fix smoke (863 matches, honest stock mage playing frostbolt/deep freeze/nova kit): arms S2 still **0.0%** — the FAIL is now cleanly the policy, not the instrument.
Smoke CSV: `ml_decisions_duel_mixed_arms_ranker.smoke-stockfix-20260801.csv`.
Next: decide the retrain scheme (win-filtered CE vs continued DAgger imitation pressure) before any further farm or gate run.

## Sparring note

Real-player challenge: pending duel set `player->duel` in `CHALLENGED`; old code treated that as in-duel and starved accept. Fixed in `cdfd75af`. Re-test sparring after that binary is installed.

## Pointers

- Stage card: [`docs/ml/curriculum/s2-spellbook-ranker.md`](../curriculum/s2-spellbook-ranker.md)
- Artifacts: [`artifacts/duel/s2/README.md`](../../../artifacts/duel/s2/README.md)
- Design: **DEC-026** in `docs/ml/DECISIONS.md`
- Orchestrator knobs: `wotlk-playerbots-server/scripts/config.ps1` (`DuelFarm*`, `-DuelMixedSeat`)

## Update 2026-08-01 (retrain session) — DEC-029 + DEC-030 landed; arms blocked on teacher projection

Retrain scheme decided and landed as **DEC-029** (`3e606318`): win-anchored self-imitation (label = own action on won episodes, else S1 expert) with 1/sqrt(freq)-balanced CE, hidden 128.
Balancing is load-bearing: unbalanced variants stayed argmax-collapsed on Auto Attack even though the label distributions are healthy.
Offline degeneracy gate (new `check_s2_argmax` scratch tool, argmax over ~20k sampled states + expert-agreement) now runs before every deploy.

Arms smoke of the dec029 heads exposed a mechanical bug, fixed as **DEC-030** (`78de2644`): the spellbook block short-circuits the tick so melee swings never started, and on-next-melee picks (Cleave) never resolved.
Auto-attack toggles (6603 + `SPELL_ATTR2_AUTO_REPEAT`: 75/5019/3018/2764) are toggle abilities, not casts; they left the candidate pool and the training labels (`--drop-labels`), and the Engine now maintains melee on the duel opponent as engagement scaffolding.

Post-DEC-030 arms smoke (fresh CSV, honest stock, tau=0, ~2.3k matches at eval): warrior WR **1.9%**, stacked FAIL.
Melee lands now (WR 0.9% -> ~2-4%), but live play is still ~72% Cleave.

**Root cause for arms, measured:** the DEC-026 DAgger expert projection is degenerate on live mixed states.
`expert_action` on the on-policy arms CSV is **79.0% Cleave r8** - on out-of-range chase ticks only on-next-melee/self casts are `isPossible`, so the queue projection collapses.
A DAgger retrain with on-policy rows upsampled x8 (`warrior.dec030b.pbml`, not deployed) reached 82.4% expert-agreement on live states and is exactly Cleave spam - imitating this teacher cannot beat stock.
Gap closers ARE in the frozen vocab (Charge 11578, Intercept 20252, Hamstring 1715), so the head can express anti-kite play; nothing teaches it.

**Next levers for arms (not started):**
1. Mixed farm with exploration (ranker seat tau ~2-3 instead of 0; needs a config knob separate from the tau=0 gate eval) + win-anchored retrain on that on-policy data - reward pressure instead of the broken imitation signal.
2. Fix the expert projection for chase states (e.g. score only currently-castable queue actions but let movement/range context in, or gate Cleave/HS expert emission on "swing timer will consume it").
3. Consider whether the 70-D features can even distinguish "at range, Intercept ready" states; if not, feature work precedes more training.

Frost smoke on the dec030 stack ran separately (see ticket for numbers).
Archives this session: `ml_decisions_duel_mixed_arms_ranker.dec029-smoke-20260801.csv`, `...dec030-smoke-20260801.csv`, heads `warrior.dec029.pbml` / `warrior.dec030b.pbml` (experiment records, untracked).

### Smoke numbers on the DEC-029/030 stack (2026-08-01, formal, tau=0, delta=0.02)

| Seat | WR | vs stock (74.6/25.8) | vs S1 (70.7/29.9) | Stacked |
|------|-----:|---------:|---------:|------|
| Arms (S2 W vs stock M) | **1.9%** (~2.3k matches) | -72.7pp FAIL | -68.8pp FAIL | **FAIL** |
| Frost (S2 M vs stock W) | **15.6%** (~2.4k matches) | -10.3pp FAIL | -14.3pp FAIL | **FAIL** |

Frost is qualitatively fixed - real rotation (Frostfire Bolt / Frost Nova / Counterspell / Frostbolt / Icy Veins / Deep Freeze / Ice Block / Cold Snap), up from 0.1% - but still below both baselines.
Arms remains mechanically able (melee lands) yet strategically stuck in Cleave spam; see the teacher-projection block above.
Smoke CSVs: `ml_decisions_duel_mixed_{arms,frost}_ranker.dec030-smoke-20260801.csv`.
Farm stopped; canonical PBMLs are the dec030 heads.
