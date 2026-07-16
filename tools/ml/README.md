# Offline duel-ranker learning

Design tracking: [`docs/ml/README.md`](../../docs/ml/README.md).

## Curriculum pipeline

1. Enable the duel bracket and logging in `playerbots.conf`:

```
AiPlayerbot.MlLoggingEnabled = 1
AiPlayerbot.MlDuelBracket.Enabled = 1
AiPlayerbot.MlDuelBracket.LogFile = "ml_decisions_duel_v2.csv"
AiPlayerbot.MlRewardDelayMs = 2000
AiPlayerbot.MlDuelBracket.TerminalLambda = 25.0
AiPlayerbot.MlDuelBracket.ActionPolicy = "softmax-stock"
AiPlayerbot.MlDuelBracket.SpellPool = "queue"
```

`softmax-stock` and `queue` are the curriculum defaults. Until the softmax ticket lands, this keeps the stock relevance
order. `random` may use `spellbook` or `union` for exploration.

2. Run bracket duels. The logger writes only valid duel decisions and backs each row with the duel outcome.

3. Train:

```bash
cd tools/ml
python train_ranker.py --csv /path/to/ml_decisions_duel_v2.csv --out duel_ranker.pbml
```

4. Deploy the single duel model:

```
AiPlayerbot.MlModelPathDuel = "/absolute/path/duel_ranker.pbml"
AiPlayerbot.MlDuelBracket.ActionPolicy = "ranker"
```

When no valid model is loaded, `ranker` falls back to stock relevance order.
