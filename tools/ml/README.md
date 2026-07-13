# Offline ML learning for playerbots

## Pipeline

1. Enable logging in `playerbots.conf`:

```
AiPlayerbot.MlLoggingEnabled = 1
AiPlayerbot.MlLogFile = "ml_decisions.csv"
AiPlayerbot.MlRewardDelayMs = 2000
```

2. Play **PvE and/or PvP**. Logger records combat decisions (meta/navigation pruned).
   Columns `in_bg` / `in_arena` mark PvP activity zones for split training.

3. Train (Python + numpy):

```bash
cd tools/ml
# Universal / hybrid (all zones, meta dropped)
python3 train_ranker.py --csv /path/to/ml_decisions.csv --out hybrid_ranker.pbml
# PvP activity only
python3 train_ranker.py --csv /path/to/ml_decisions.csv --out pvp_ranker.pbml --pvp-only
# Open-world / dungeon PvE only
python3 train_ranker.py --csv /path/to/ml_decisions.csv --out pve_ranker.pbml --pve-only
```

4. Deploy and blend:

```
AiPlayerbot.MlModelPathHybrid = "/absolute/path/hybrid_ranker.pbml"
AiPlayerbot.MlModelPathPvp = "/absolute/path/pvp_ranker.pbml"
AiPlayerbot.MlHybridAlpha = 0.3
AiPlayerbot.MlPvpAlpha = 0.3
```

Raise alpha toward `1.0` as metrics improve. `0.0` = heuristics only.

`--pve-only` models deploy via `MlModelPathHybrid` (open-world/dungeon scorer).
There is no separate `MlModelPathPve` yet — hybrid is the universal / PvE slot.

## Schema (v2)

| Block | Size | Notes |
|---|---|---|
| Features `f0..f11` | 12 | HP, casting, **enemy casting heal (spell-agnostic)**, BG/arena, … |
| Flags `a0..a7` | 8 | interrupt, healer-focus, defensive, CC, heal, instant, **damage**, **focus player** |
| Total | **20** | Old 18-D PBML1 still loads (extra flags ignored) |

## Files

| Piece | Role |
|---|---|
| `src/Ai/Ml/MlDecisionLogger.*` | Log + delayed rewards (meta pruned; survival rebalanced) |
| `src/Ai/Ml/CombatDecisionFeatures.*` | Features + action taxonomy |
| `src/Ai/Ml/MlMlpModel.*` | PBML1 forward pass |
| `src/Ai/Ml/MlScorer.*` | Blend heuristic ↔ MLP |
| `train_ranker.py` | Offline SGD (`--pvp-only` / `--pve-only` / meta filter) |

## Notes

- No backprop on the map thread.
- Unknown/missing model → heuristics only.
- After logger upgrades, archive old CSV and start a fresh `ml_decisions.csv` (header `a0..a7`).
