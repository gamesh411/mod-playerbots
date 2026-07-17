# S1 bootstrap models (pre-freeze)

Reward-regression bootstrap from Softmax-stock `ml_decisions_duel_v3.csv`
(250k rows/class after filters, 30 epochs, 78-D PBML1).

| File | Class | Train seed |
|------|-------|------------|
| `warrior.pbml` | Warrior | 1 |
| `mage.pbml` | Mage | 2 |

**Not a stage freeze.** DAgger×2 + expert-off + stock↔stock eval still required
([#18](https://github.com/gamesh411/mod-playerbots/issues/18) / DEC-025).

Deploy:

```
AiPlayerbot.MlDuelBracket.ActionPolicy = "ranker"
AiPlayerbot.MlDuelBracket.SpellPool = "queue"
AiPlayerbot.MlDuelBracket.SoftmaxTemperature = 10.0
AiPlayerbot.MlModelPathDuel.Warrior = "<abs>/artifacts/duel/s1/warrior.pbml"
AiPlayerbot.MlModelPathDuel.Mage = "<abs>/artifacts/duel/s1/mage.pbml"
```

Requires a worldserver built with the S1 Softmax-ranker + per-class loader.
