# Offline duel-ranker learning

Design tracking: [`docs/ml/README.md`](../../docs/ml/README.md).

## Curriculum pipeline

1. Enable the duel bracket and logging in `playerbots.conf`:

```
AiPlayerbot.MlLoggingEnabled = 1
AiPlayerbot.MlDuelBracket.Enabled = 1
AiPlayerbot.MlDuelBracket.LogFile = "ml_decisions_duel_v4.csv"
AiPlayerbot.MlRewardDelayMs = 2000
AiPlayerbot.MlDuelBracket.TerminalLambda = 25.0
AiPlayerbot.MlDuelBracket.ActionPolicy = "softmax-stock"
AiPlayerbot.MlDuelBracket.SpellPool = "queue"
AiPlayerbot.MlDuelBracket.SoftmaxTemperature = 10.0
```

`softmax-stock` + `queue` are the S0 curriculum defaults (DEC-022): Softmax(τ) over stock scripted-queue
relevance (τ=10 farm; τ≤0 argmax for demo). `random` may use `spellbook` or `union` for archived ablations.

`duel_v4` adds `expert_action` (Softmax-stock τ=0 pick) for DAgger (DEC-025). Headerless `duel_v3` files are still accepted by the trainer.

2. Run bracket duels. The logger writes only valid duel decisions and backs each row with the duel outcome.

3. Bootstrap train (S1, per-class):

```bash
cd tools/ml
python train_ranker.py --csv /path/to/ml_decisions_duel_v3.csv --out ../../artifacts/duel/s1/warrior.pbml \
  --duel-only --drop-duel-noise --self-class warrior
python train_ranker.py --csv /path/to/ml_decisions_duel_v3.csv --out ../../artifacts/duel/s1/mage.pbml \
  --duel-only --drop-duel-noise --self-class mage
```

4. Deploy per-class S1 models (**DEC-025**):

```
AiPlayerbot.MlDuelBracket.ActionPolicy = "ranker"
AiPlayerbot.MlDuelBracket.SpellPool = "queue"
AiPlayerbot.MlDuelBracket.SoftmaxTemperature = 10.0   # farm; use 0 for demo/freeze
AiPlayerbot.MlModelPathDuel.Warrior = "/absolute/path/artifacts/duel/s1/warrior.pbml"
AiPlayerbot.MlModelPathDuel.Mage = "/absolute/path/artifacts/duel/s1/mage.pbml"
```

Train recipe (S1): reward bootstrap on Softmax-stock CSV → two DAgger rounds (imitate Softmax-stock τ=0 on ranker states; needs `expert_action` / v4) → expert-off reward until both seats clear stock↔stock winrate uplift. Aggregate all rows each retrain.

When no valid model is loaded for a bot's class, `ranker` falls back to stock relevance order.
