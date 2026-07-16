# Offline ML learning for playerbots

**Curriculum hub (primary):** [`docs/ml/README.md`](../../docs/ml/README.md) — duel showcase **S0 → S1 → S2**.  
**Archive** (Mode A / arena-first / pure-random notes): [`docs/ml/archive/README.md`](../../docs/ml/archive/README.md).  
Governance: [`docs/ml/DECISIONS.md`](../../docs/ml/DECISIONS.md) (**DEC-018**), [`docs/ml/FEATURES.md`](../../docs/ml/FEATURES.md).

## Duel curriculum (preferred)

```bash
# Analyze / train on duel logs (schema auto-detects f0..fN)
python analyze_duel_ranker.py --csv /path/to/ml_decisions_duel_v2.csv
python train_ranker.py --csv /path/to/ml_decisions_duel_v2.csv --out duel_ranker.pbml \
  --duel-only --terminal-only --drop-duel-noise
```

Stage policies and freeze tags: [`docs/ml/curriculum/`](../../docs/ml/curriculum/).

## Legacy arena / hybrid Mode A pipeline

Historical trainer path (still useful as warm-start compare; not showcase S0):

1. Enable logging in `playerbots.conf` (tournament profile sets these):

```
AiPlayerbot.MlLoggingEnabled = 1
AiPlayerbot.MlLogFile = "ml_decisions_v3.csv"
AiPlayerbot.MlRewardDelayMs = 2000
AiPlayerbot.MlTerminalLambda = 1.0
AiPlayerbot.MlExploreEpsilon = 0.08
AiPlayerbot.MlExploreArenaOnly = 1
```

2. Train:

```bash
cd tools/ml
python train_ranker.py --csv /path/to/ml_decisions_v3.csv --out pvp_ranker.pbml --arena-only
```

3. Deploy (alpha 0 until input_dim matches current feature layout — see DEC-017 / 78-D):

```
AiPlayerbot.MlModelPathPvp = "/absolute/path/pvp_ranker.pbml"
AiPlayerbot.MlPvpAlpha = 0.0
```
