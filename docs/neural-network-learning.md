# How Would This Solution Learn?

Short answer: **offline**. The live `worldserver` collects experience as logged decisions; models train **outside** the map thread; updated weights (PBML1) are loaded into Option B/C scorers. Bots do **not** run gradient descent during combat.

## Implementation status

| Piece | Status |
|---|---|
| Decision logging + delayed rewards | **Implemented** (`MlDecisionLogger`) |
| Heuristic B/C scorers | **Implemented** (`HeuristicScores`) |
| Tiny MLP inference (PBML1) | **Implemented** (`MlMlpModel` / `MlScorer`) |
| Alpha blend heuristic↔MLP | **Implemented** (`MlHybridAlpha` / `MlPvpAlpha`) |
| Offline trainer | **Implemented** (`tools/ml/train_ranker.py`) |
| Engine + UpdateAI hooks | **Implemented** |

See `tools/ml/README.md` for enable → play → train → deploy steps.

```
┌─────────────┐   logs    ┌──────────────┐  train   ┌─────────────┐  PBML1  ┌──────────────────┐
│ Live bots   │ ────────► │ ml_decisions │ ───────► │ train_ranker│ ──────► │ HybridRelevance /│
│ Engine tick │           │ .csv         │          │ .py         │         │ PvpPolicy scorer │
└─────────────┘           └──────────────┘          └─────────────┘         └──────────────────┘
       ▲                                                         │
       └────────────────── reload model file ◄───────────────────┘
```

---

## 1. What Gets Logged (the “experience”)

Hook near `Engine::DoNextAction` after an action succeeds; reward resolved ~`MlRewardDelayMs` later in `PlayerbotAI::UpdateAI`:

| Field | Purpose |
|---|---|
| features f0..f11 | `CombatFeatureVector` (heal-cast = any positive spell) |
| action flags a0..a7 | interrupt / healer-focus / defensive / CC / heal / instant / damage / focus-player |
| `in_bg` / `in_arena` | PvP activity zone (train hybrid, `--pvp-only`, or `--pve-only`) |
| chosen action | label context (meta/navigation pruned) |
| heuristic / final score | baseline |
| **reward** | interrupt (+heal stop bonus), HP deltas, kill, healer pressure; mild survival |

Default: mastered bots, BG/arena, or anyone in combat (`MlLogAllBots=0`).

---

## 2. Learning Modes

### Mode A — Reward regression (shipped trainer)

`train_ranker.py` fits MLP to predict delayed `reward` from `(features, action_flags)`.

Deployed score = sigmoid-mapped prediction blended with heuristics via alpha.

### Mode B — Imitation / ranking (next iteration)

Filter winning episodes; train preference among legal actions (extend trainer).

### Mode C — Offline RL / self-play (later)

Conservative RL on stored logs; bot-vs-bot arenas for more data.

---

## 3. Config knobs

```
AiPlayerbot.MlLoggingEnabled = 1
AiPlayerbot.MlLogFile = "ml_decisions.csv"
AiPlayerbot.MlRewardDelayMs = 2000
AiPlayerbot.MlModelPathHybrid = "/path/hybrid_ranker.pbml"
AiPlayerbot.MlModelPathPvp = "/path/pvp_ranker.pbml"
AiPlayerbot.MlHybridAlpha = 0.3
AiPlayerbot.MlPvpAlpha = 0.3
```

Alpha `0` = heuristics only (safe default).

---

## 4. What Does *Not* Learn Live

No backprop on map thread, no random exploration in real arenas, no per-bot online fine-tuning.

---

## Bottom Line

**Flight recorder on the server, flight school offline, new brain file back into B/C** — now wired in-tree.
