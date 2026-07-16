# Offline ML learning for playerbots

Design tracking: [`docs/ml/README.md`](../../docs/ml/README.md) (directions, decisions, features).

## Pipeline

1. Enable logging in `playerbots.conf` (tournament profile sets these):

```
AiPlayerbot.MlLoggingEnabled = 1
AiPlayerbot.MlLogFile = "ml_decisions_v3.csv"
AiPlayerbot.MlRewardDelayMs = 2000
AiPlayerbot.MlTerminalLambda = 1.0
AiPlayerbot.MlExploreEpsilon = 0.08
AiPlayerbot.MlExploreArenaOnly = 1
```

2. Play **PvE and/or PvP**. Logger records combat decisions (meta/navigation pruned).
   Columns `in_bg` / `in_arena` / `match_id` / `terminal` / `explored` support splits and credit assignment.
   **Rotate the log filename** when columns change (`v2`, `v3`, …).

3. Train (Python + numpy):

```bash
cd tools/ml
# Universal / hybrid (all zones, meta dropped)
python train_ranker.py --csv /path/to/ml_decisions.csv --out hybrid_ranker.pbml
# All PvP (BG + arena)
python train_ranker.py --csv /path/to/ml_decisions_v3.csv --out pvp_ranker.pbml --pvp-only
# Rated-arena specialist (unified 2v2+3v3)
python train_ranker.py --csv /path/to/ml_decisions_v3.csv --out pvp_ranker.pbml --arena-only
# Open-world / dungeon PvE only
python train_ranker.py --csv /path/to/ml_decisions.csv --out pve_ranker.pbml --pve-only
```

4. Deploy and blend:

```
AiPlayerbot.MlModelPathHybrid = "/absolute/path/hybrid_ranker.pbml"
AiPlayerbot.MlModelPathPvp = "/absolute/path/pvp_ranker.pbml"
AiPlayerbot.MlHybridAlpha = 0.15
AiPlayerbot.MlPvpAlpha = 0.15
```

Raise alpha toward `1.0` only after explore+terminal retrains (see DEC-006). `0.0` = heuristics only.

`--pve-only` models deploy via `MlModelPathHybrid` (open-world/dungeon scorer).
There is no separate `MlModelPathPve` yet — hybrid is the universal / PvE slot.
