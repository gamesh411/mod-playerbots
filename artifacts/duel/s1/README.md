# S1 models (DEC-025 execute — soft-fail, DEC-028)

| File | Class | Stage |
|------|-------|--------|
| `warrior.pbml` | Warrior | Round-2 82-D (canonical; arms gate FAIL) |
| `mage.pbml` | Mage | Round-2 82-D (canonical; frost gate PASS +5.2pp) |
| `warrior.round3-fail-20260728.pbml` | Warrior | Round-3 DAgger+expert-off (arms −5.7pp) |
| `mage.round3-fail-20260728.pbml` | Mage | Round-3 DAgger+expert-off (frost −11.6pp) |
| `baseline_stock_stock.md` | — | S0 Softmax-stock seat WRs |

**PBML `input_dim = 82`** = 70 state + 8 role flags + 4 FNV action-id.

Teaching write-up: [docs/ml/research/s1-training-lessons-learned.md](../../../docs/ml/research/s1-training-lessons-learned.md).

## Freeze eval (δ=0.02)

| Seat | WR | vs stock | n | Notes |
|------|-----:|---------:|--:|-------|
| Stock↔stock | W 74.6% / M 25.8% | — | ~45k | baseline |
| Frost-ranker (round-2) | **31.0%** | **+5.2pp** | **~2368** | **PASS** |
| Arms-ranker (round-2) | **70.9%** | **−3.7pp** | **2502** | **FAIL** |
| Arms-ranker (round-3) | **68.9%** | **−5.7pp** | **2482** | **FAIL** |
| Frost-ranker (round-3) | **14.2%** | **−11.6pp** | **2379** | **FAIL** (regressed) |

**DEC-025 mixed gate:** not clear. **DEC-028:** soft-fail waiver; no `stage/s1` tag; pivot to S2 execute (#19).
