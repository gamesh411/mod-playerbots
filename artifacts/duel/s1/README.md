# S1 models (DEC-025 execute — pre-freeze)

| File | Class | Stage |
|------|-------|--------|
| `warrior.pbml` | Warrior | 82-D DAgger (v3+v4) |
| `mage.pbml` | Mage | 82-D DAgger v4 + fixed expert elev (**no expert-off**) |
| `baseline_stock_stock.md` | — | S0 Softmax-stock seat WRs |

**PBML `input_dim = 82`** = 70 state + 8 role flags + 4 FNV action-id.

Teaching write-up of novel failure modes: [docs/ml/research/s1-training-lessons-learned.md](../../../docs/ml/research/s1-training-lessons-learned.md).

## Fixes landed this session (uncommitted on `exp/duel-rl-curriculum`)

1. **Expert elevation** (`train_ranker.py`) — always elevate Softmax-stock expert to `imitate_target`, including when learner already matched (losing seat was training teacher toward −λ).
2. **Action-id pack** (Engine + trainer + FEATURES.md) — FNV name fingerprint so frostbolt ≠ fireball (was 200/200 identical ScoreDuel → ~6% WR).
3. **Logger width guard** (`MlDecisionLogger.cpp`) — headerless 90-col v4 detected by column count so a restart cannot silently downgrade to 89-col mid-file (shifted `terminal`, froze eval at ~142 matches).

## Freeze eval (δ=0.02)

| Seat | WR | vs stock | n | Notes |
|------|-----:|---------:|--:|-------|
| Stock↔stock | W 74.6% / M 25.8% | — | ~45k | baseline |
| Frost-ranker (78-D, broken) | ~6–9% | −16…−19pp | ~3k | flag collision |
| Frost-ranker (82-D, thin) | 26.8% | +0.9pp | 142 | dirty / lucky |
| Frost-ranker (82-D, clean) | **31.0%** | **+5.2pp** | **~2368** | **PASS** (need ≥27.8%); CSV `ml_decisions_duel_mixed_frost_ranker.csv` |
| Arms-ranker (82-D) | **70.9%** | **−3.7pp** | **2502** | **FAIL** (need ≥76.6%); CSV `ml_decisions_duel_mixed_arms_ranker.csv` |

**DEC-025 mixed gate:** not clear — frost PASS, arms FAIL.

## Ops now

Arms seat is the blocker. Options:

1. More warrior DAgger (retrain on `duel_v4` / further on-policy) → re-run arms mixed eval.
2. Accept S1 soft-fail on warrior and document frost-only clear (human call).

Do not freeze / tag / close #18 until both seats clear (or soft-fail accepted) and DEC-019 packaging (manifest/tag/release) is done.
