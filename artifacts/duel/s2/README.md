# S2 models (DEC-026 execute — not frozen)

| File | Class | Notes |
|------|-------|--------|
| `warrior.pbml` | Warrior | DEC-033 round-2 win-anchored (`dec033r2`, 2026-08-02); `in=70` `out=47` (stances out per DEC-034) |
| `mage.pbml` | Mage | DEC-033 round-2 win-anchored (`dec033r2`, 2026-08-02); `in=70` `out=220` (incl. Freeze 33395) |
| `vocab.warrior.txt` | Warrior | Frozen spell-id list (49) used from DAgger r2 onward |
| `vocab.mage.txt` | Mage | Frozen spell-id list (220) |

**Freeze status:** not cut. Latest stacked smoke (2026-08-02): Arms 59.7% / Frost 24.2% vs stock 74.6/25.8 — both seats still FAIL, frost within 1.6pp of stock baseline. Round trend Arms 1.9→33.8→59.7, Frost 15.6→16.5→24.2. See [`docs/ml/research/handoff-2026-08-02-dec033-round2-smokes.md`](../../docs/ml/research/handoff-2026-08-02-dec033-round2-smokes.md).

Local-only checkpoints (often untracked): `*.pre-*`, `*.dec0NN*` (weights before/at each retrain).

## Train / deploy

Farm CSVs on the server host (aggregate all rounds):

- `ml_decisions_duel_s2.csv` — bootstrap
- `ml_decisions_duel_s2_r2.csv` / `_r3.csv` — DAgger
- `ml_decisions_duel_s2_eo.csv` — expert-off

```bash
cd tools/ml
# DAgger (imitate S1 teacher labels in expert_action)
python train_spellbook_ranker.py --csv .../s2.csv .../s2_r2.csv \
  --out ../../artifacts/duel/s2/warrior.pbml --self-class warrior \
  --duel-only --drop-duel-noise --imitate-expert \
  --vocab-file ../../artifacts/duel/s2/vocab.warrior.txt --epochs 30 --max-rows 400000

# Expert-off (no --imitate-expert)
python train_spellbook_ranker.py --csv .../s2.csv .../s2_r2.csv .../s2_r3.csv .../s2_eo.csv \
  --out ../../artifacts/duel/s2/warrior.pbml --self-class warrior \
  --duel-only --drop-duel-noise \
  --vocab-file ../../artifacts/duel/s2/vocab.warrior.txt --epochs 30 --max-rows 400000
```

Deploy: `MlModelPathDuel.Warrior` / `.Mage` → these PBMLs; keep `MlModelPathDuelTeacher.*` on S1 round-2 for DAgger.

## Freeze gate (DEC-026 stacked)

```bash
python eval_duel_winrate.py --csv ml_decisions_duel_mixed_arms_ranker.csv --ranker-seat warrior \
  --baseline-csv ml_decisions_duel_v3.csv --baseline-csv-s1 ml_decisions_duel_v4.csv --delta 0.02
python eval_duel_winrate.py --csv ml_decisions_duel_mixed_frost_ranker.csv --ranker-seat mage \
  --baseline-csv ml_decisions_duel_v3.csv --baseline-csv-s1 ml_decisions_duel_v4.csv --delta 0.02
```

First measured gate (2026-07-29): both seats FAIL (see handoff). Do not cut `stage/s2` until diagnosis + re-eval.

## DEC-027

Confirm `Playerbots.log` shows `DEC-027 Freeze 33395 Targets=...` at bracket load and
`DEC-027 Freeze cast OK` when Water Elemental Freeze is issued. Farm action counts for 33395 stayed ~0 through expert-off.
