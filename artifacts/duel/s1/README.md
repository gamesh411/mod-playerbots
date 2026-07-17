# S1 models (DEC-025 execute — pre-freeze)

| File | Class | Stage |
|------|-------|--------|
| `warrior.pbml` | Warrior | expert-off aggregate (v3+v4), 250k subsample |
| `mage.pbml` | Mage | expert-off aggregate (v3+v4), 250k subsample |
| `baseline_stock_stock.md` | — | S0 Softmax-stock seat WRs |

## Train path taken

1. **Bootstrap** — reward on `ml_decisions_duel_v3.csv`
2. **DAgger** — `ActionPolicy=ranker` farm → `ml_decisions_duel_v4.csv` (`expert_action`); score-up Softmax-stock τ=0
3. **Expert-off** — reward-only retrain on aggregate v3+v4

## Eval snapshot (ranker↔ranker farm, not freeze gate)

| Seat | Stock↔stock (v3) | Ranker↔ranker (v4) |
|------|-----------------:|-------------------:|
| Warrior | 74.6% | 92.9% |
| Mage | 25.8% | 7.3% |

DEC-025 freeze needs **mixed** seats (Arms-ranker↔Frost-stock and reverse) + δ.
Same-policy WR here is diagnostic only.

## Deploy

```
AiPlayerbot.MlDuelBracket.ActionPolicy = "ranker"
AiPlayerbot.MlDuelBracket.SpellPool = "queue"
AiPlayerbot.MlDuelBracket.SoftmaxTemperature = 10.0   # 0 for demo
AiPlayerbot.MlModelPathDuel.Warrior = "<abs>/artifacts/duel/s1/warrior.pbml"
AiPlayerbot.MlModelPathDuel.Mage = "<abs>/artifacts/duel/s1/mage.pbml"
AiPlayerbot.MlDuelBracket.LogFile = "ml_decisions_duel_v4.csv"
```

Live farm: `ensure_running.ps1 -ProfileGB 32 -ServerProfile duel-farm -RestartServers`
