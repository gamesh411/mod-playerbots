# S2 models (DEC-026 execute — not frozen)

| File | Class | Stage |
|------|-------|--------|
| `warrior.pbml` | Warrior | _TBD multi-logit after S2 farm + train_ |
| `mage.pbml` | Mage | _TBD multi-logit after S2 farm + train_ |
| `vocab.warrior.txt` | Warrior | Optional frozen class@80 spell-id list |
| `mage.vocab.txt` / `vocab.mage.txt` | Mage | Optional frozen class@80 spell-id list |

## Train / deploy

Bootstrap farm writes spell-id `action` / S1-teacher `expert_action` to `ml_decisions_duel_s2.csv`
(`ActionPolicy=ranker`, `SpellPool=spellbook`, empty learner paths ⇒ uniform Softmax explore).

```bash
cd tools/ml
python train_spellbook_ranker.py --csv /path/ml_decisions_duel_s2.csv \
  --out ../../artifacts/duel/s2/warrior.pbml --self-class warrior --duel-only
python train_spellbook_ranker.py --csv /path/ml_decisions_duel_s2.csv \
  --out ../../artifacts/duel/s2/mage.pbml --self-class mage --duel-only --imitate-expert
```

Deploy: set `MlModelPathDuel.Warrior` / `.Mage` to these PBMLs; keep
`MlModelPathDuelTeacher.*` on S1 round-2 for DAgger rounds.

## Freeze gate (DEC-026 stacked)

Both seats must clear **S1↔S1 + δ** *and* **stock↔stock + δ**:

```bash
python eval_duel_winrate.py --csv mixed_s2_arms.csv --ranker-seat warrior \
  --baseline-csv ml_decisions_duel_v3.csv --baseline-csv-s1 ml_decisions_duel_v4.csv --delta 0.02
```

## DEC-027

Confirm `Playerbots.log` shows `DEC-027 Freeze 33395 Targets=...` at bracket load and
`DEC-027 Freeze cast OK` when Water Elemental Freeze is issued.
