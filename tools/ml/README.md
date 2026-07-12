# Offline ML learning for playerbots

## Pipeline

1. Enable logging in `playerbots.conf`:

```
AiPlayerbot.MlLoggingEnabled = 1
AiPlayerbot.MlLogFile = "ml_decisions.csv"
AiPlayerbot.MlRewardDelayMs = 2000
```

2. Play (prefer arenas/BGs with mastered bots). Each successful action is logged; ~2s later a reward is appended.

3. Train on a machine with Python + numpy (your GTX 1080 is optional here — this MLP is tiny):

```bash
cd tools/ml
python3 train_ranker.py --csv /path/to/ml_decisions.csv --out hybrid_ranker.pbml
python3 train_ranker.py --csv /path/to/ml_decisions.csv --out pvp_ranker.pbml --pvp-only
```

4. Point the server at the models and blend gradually:

```
AiPlayerbot.MlModelPathHybrid = "/absolute/path/hybrid_ranker.pbml"
AiPlayerbot.MlModelPathPvp = "/absolute/path/pvp_ranker.pbml"
AiPlayerbot.MlHybridAlpha = 0.3
AiPlayerbot.MlPvpAlpha = 0.3
```

Restart (or ensure config reload path calls `sMlScorer.Reload()`). Raise alpha toward `1.0` as winrate/interrupt metrics improve. `0.0` keeps pure heuristics.

## Files

| Piece | Role |
|---|---|
| `src/Ai/Ml/MlDecisionLogger.*` | Log + delayed rewards |
| `src/Ai/Ml/MlMlpModel.*` | PBML1 forward pass |
| `src/Ai/Ml/MlScorer.*` | Blend heuristic ↔ MLP |
| `src/Ai/Ml/HeuristicScores.*` | Current B/C rules |
| `train_ranker.py` | Offline SGD trainer |

## Notes

- No backprop on the map thread.
- Unknown/missing model → heuristics only.
- Input vector = 12 combat features + 6 action-type flags (18 floats).
