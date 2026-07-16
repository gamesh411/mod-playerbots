# How Would This Solution Learn?

Short answer: **offline**. The live `worldserver` collects experience as logged decisions; models train **outside** the map thread; updated weights (PBML1) are loaded into Option B/C scorers. Bots do **not** run gradient descent during combat.

**Project tracking (curriculum hub):** see [`../README.md`](../README.md). Archived directions: [`DIRECTIONS-pre-curriculum.md`](DIRECTIONS-pre-curriculum.md).

## Implementation status

| Piece | Status |
|---|---|
| Decision logging + delayed short rewards | **Implemented** (`MlDecisionLogger`) |
| Match `match_id` + terminal win/loss backup | **Implemented** (arena/BG end) |
| ε-greedy explore on legal combat queue actions | **Implemented** (`MlExploreEpsilon`) |
| Heuristic B/C scorers | **Implemented** (`HeuristicScores`) |
| Tiny MLP inference (PBML1) | **Implemented** (`MlMlpModel` / `MlScorer`) |
| Alpha blend heuristic↔MLP | **Implemented** (`MlHybridAlpha` / `MlPvpAlpha`) |
| Offline trainer | **Implemented** (`tools/ml/train_ranker.py`, `--arena-only`) |
| Engine + UpdateAI hooks | **Implemented** |
| Duels-first feature packs / difficulty dial | **Documented next** ([DIRECTIONS D6–D8](ml/DIRECTIONS.md)) |
| Duel bracket (idle → duel, config pairs) | **In progress** ([DEC-012](ml/DECISIONS.md) / D12) |
| Action-head / ranking net | **Primary arch** ([DEC-011](ml/DECISIONS.md) / D9); Mode A remains baseline |

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
| `in_bg` / `in_arena` | PvP activity zone (train hybrid, `--pvp-only`, `--arena-only`, or `--pve-only`) |
| `match_id` / `terminal` / `short_reward` | Match credit (DEC-001/003) |
| `explored` | ε-greedy bit (DEC-005) |
| chosen action | label context (meta/navigation pruned) |
| heuristic / final score | baseline |
| **reward** | `short_reward + λ * terminal` (plus interrupt/HP/kill shaping inside short) |

Default: mastered bots, BG/arena, or anyone in combat (`MlLogAllBots=0`).

---

## 2. Learning Modes

### Mode A — Reward regression (shipped trainer; **baseline only**)

`train_ranker.py` fits MLP to predict delayed `reward` from `(features, action_flags)`.

Deployed score = sigmoid-mapped prediction blended with heuristics via alpha.

**Superseded as primary architecture by Mode A′ / D9** (DEC-011). Keep Mode A weights for warm-start and A/B compares.

### Mode A′ — Action-head / ranking net (**primary target**, DEC-011)

One forward over **state** → scores/logits for **all legal actions** in the Engine queue; pick argmax (ε-greedy / softmax for explore). Trainer will move from scalar reward regression to a ranking / preference loss once duel episodes (D12) are dense.

### Mode B — Imitation / ranking (next iteration)

Filter winning episodes; train preference among legal actions (extends Mode A′).

### Mode C — Offline RL / self-play (later)

Conservative RL on stored logs; bot-vs-bot arenas / duel bracket for more data.

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

No backprop on map thread, no per-bot online fine-tuning. Arena still uses bounded ε-greedy on top of heuristics; **duels** use legal-action pool only (DEC-013) — no heuristic pick as teacher.

---

## Bottom Line

**Flight recorder on the server, flight school offline, new brain file back into B/C** — now wired in-tree.
