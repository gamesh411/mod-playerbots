# S1 models (DEC-025 execute — pre-freeze)

| File | Class | Stage |
|------|-------|--------|
| `warrior.pbml` | Warrior | DAgger×2 + expert-off aggregate (v3+v4), 250k subsample |
| `mage.pbml` | Mage | DAgger×2 + expert-off aggregate (v3+v4), 250k subsample |
| `baseline_stock_stock.md` | — | S0 Softmax-stock seat WRs |

## Train path taken

1. **Bootstrap** — reward on `ml_decisions_duel_v3.csv`
2. **DAgger ×2** — `ActionPolicy=ranker` farm → `ml_decisions_duel_v4.csv` (`expert_action`); score-up Softmax-stock τ=0
3. **Expert-off** — reward-only retrain on aggregate v3+v4 (after each DAgger round)

## Eval snapshot

| Seat / mode | WR | Notes |
|------|-----:|-------|
| Warrior stock↔stock (v3) | 74.6% | freeze baseline |
| Mage stock↔stock (v3) | 25.8% | freeze baseline |
| Warrior ranker↔ranker (v4) | ~93% | diagnostic only |
| Mage ranker↔ranker (v4) | ~7% | diagnostic only |
| Arms-ranker↔Frost-stock (pre-redeploy, ~3.4k) | **74.3%** (−0.3pp) | FAIL δ=0.02 — needs more train / farm |
| Frost-ranker↔Arms-stock | _farming_ | `ml_decisions_duel_mixed_frost_ranker.csv` |

δ = **0.02**. Mixed path: Engine empty-class → Softmax-stock; `-DuelMixedSeat arms-ranker|frost-ranker`.

## Deploy

```
AiPlayerbot.MlDuelBracket.ActionPolicy = "ranker"
AiPlayerbot.MlDuelBracket.SpellPool = "queue"
AiPlayerbot.MlDuelBracket.SoftmaxTemperature = 10.0   # 0 for demo / mixed eval
AiPlayerbot.MlModelPathDuel.Warrior = "<abs>/artifacts/duel/s1/warrior.pbml"
AiPlayerbot.MlModelPathDuel.Mage = "<abs>/artifacts/duel/s1/mage.pbml"
AiPlayerbot.MlDuelBracket.LogFile = "ml_decisions_duel_v4.csv"
```

DAgger farm: `ensure_running.ps1 -ProfileGB 32 -ServerProfile duel-farm -RestartServers`  
Mixed freeze eval: add `-DuelMixedSeat arms-ranker` or `frost-ranker`
