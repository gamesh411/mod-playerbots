# How Would This Solution Learn?

Short answer: **offline**. The live `worldserver` collects experience as logged decisions; models train **outside** the map thread (on your GTX 1080 or a training box); updated weights are exported (ONNX) and loaded back into Option B/C multipliers. Bots do **not** run gradient descent during combat.

```
┌─────────────┐   logs    ┌──────────────┐  train   ┌─────────────┐  ONNX   ┌──────────────────┐
│ Live bots   │ ────────► │ Dataset      │ ───────► │ Trainer     │ ──────► │ HybridRelevance /│
│ Engine tick │           │ (parquet/csv)│          │ (PyTorch)   │         │ PvpPolicy scorer │
└─────────────┘           └──────────────┘          └─────────────┘         └──────────────────┘
       ▲                                                         │
       └────────────────── reload model file ◄───────────────────┘
```

---

## 1. What Gets Logged (the “experience”)

Hook near `Engine::DoNextAction` / `ListenAndExecute` (after an action succeeds):

| Field | Example | Purpose |
|---|---|---|
| `timestamp`, `bot_guid`, `map`, `bg/arena` | … | Filter datasets |
| `features[]` | current `CombatFeatureVector` (+ expanded) | NN input |
| `legal_actions[]` | names/`isPossible` set this tick | Action mask |
| `chosen_action` | `"mind freeze on enemy healer"` | Label |
| `rule_relevance` | pre-multiplier score | Analysis |
| `heuristic_multiplier` | B/C score used | Baseline |
| **Delayed outcomes** (0.5s / 2s / fight end) | see below | Reward / ranking |

### Delayed outcomes (rewards)

Logged a few seconds later or at match end:

| Signal | Good for |
|---|---|
| Interrupt landed / cast interrupted | NN-3 kick head |
| Target died within 3s of swap | Kill-window net |
| Ally survived lethal window after defensive | Defensive use |
| Healing effective (HP recovered, death prevented) | Triage net |
| Arena/BG win, personal rating proxy | Policy net |
| Damage on enemy healer | Focus fire |

**Important:** rewards are attached to *past* decisions (credit assignment), not computed inside `GetValue()`.

### Where data comes from

| Source | Quality | Volume |
|---|---|---|
| Mastered bots in arena with skilled players | Highest | Low |
| Bot-vs-bot arenas with self-play | Medium–high | High |
| Random BG bots | Noisy | Very high |
| Scripted “expert” heuristics shadowing | Medium | Controlled |

Start with **mastered arena + self-play**; treat raw random-BG logs as weak unlabeled data.

Existing `sPlayerbotAIConfig.log(...)` CSV helpers can bootstrap; later use a binary/parquet writer for volume.

---

## 2. Learning Modes (what “train” means)

### Mode A — Imitation / Behavior Cloning (first to ship)

**Idea:** Copy decisions from “good” episodes.

1. Filter logs where the team won, or where a human master was present.
2. For each tick, treat `chosen_action` as the label among `legal_actions`.
3. Train NN-1 with **cross-entropy** / ranking loss:

\[
\mathcal{L} = -\log p_\theta(\text{chosen} \mid features, legal\_mask)
\]

**What it learns:** “In states like this, good players kicked / trinketed / swapped.”

**Limits:** Clones mistakes in the data; cannot invent better play than the demonstrator.

**Concrete example:**  
10k winning 2v2 rounds where resto+rogue beat caster teams → network learns to prefer `kick` on Poly over `blind` when partner has no spell ward.

---

### Mode B — Preference / Ranking Learning

**Idea:** Don’t only clone the winner’s action; learn **which of two actions was better**.

1. From the same state, compare action A (taken) vs B (legal but not taken).
2. If outcome was good (interrupt landed, win), prefer A ≻ B; if bad, invert or down-weight.
3. Train with pairwise logistic / ListNet loss.

**Fits Option B naturally:** output is a relevance multiplier, not a hard one-hot policy.

---

### Mode C — Offline Reinforcement Learning (after A/B)

**Idea:** Improve beyond demonstrators using stored rewards — **without** exploring live on players.

Algorithms that fit private-server logs:

| Algo | Role |
|---|---|
| **CQL / IQL / TD3+BC** | Conservative Q-learning from fixed dataset |
| **Advantage-weighted BC** | Up-weight actions that led to high return |
| **Decision Transformer** (optional) | Condition policy on return-to-go |

**Reward sketch (arena):**

```
r_t = +2.0 * interrupt_success
    + 1.5 * damage_to_healer_norm
    + 3.0 * enemy_kill
    - 4.0 * ally_death
    - 0.1 * pointless_gcd   # optional shaping
    + 10.0 * match_win (terminal)
```

**Why offline:** Online RL (ε-greedy kicks in live arenas) griefs players and destabilizes the server. Explore in **bot-vs-bot** or shadow mode only.

---

### Mode D — Self-Play Loop

1. Population of bots loads current ONNX policy.
2. Run thousands of arena simulations (no real players).
3. Winners’ trajectories enter the dataset; losers down-weighted.
4. Retrain → evaluate vs previous checkpoint → promote if winrate ↑.

This is how Option C (policy net) improves after the imitation bootstrap.

---

## 3. Training Pipeline (on your hardware)

| Step | Where | Notes |
|---|---|---|
| Collect logs | `worldserver` (CPU) | Append-only; never block map thread (async queue) |
| Ship / sync logs | disk / object store | Nightly job |
| Train | **GTX 1080** + PyTorch | Hours for small MLPs; fine for &lt;1M params |
| Validate | held-out matches | Winrate, interrupt precision, action KL vs heuristic |
| Export | `torch.onnx.export` → `hybrid_ranker.onnx` | |
| Deploy | copy into server data dir; config path | Hot-reload or restart |
| Fallback | if load/infer fails → current C++ heuristics | |

**Your i7-8700** runs inference only. **1080** is for training (and optional offline eval), not per-GCD combat.

---

## 4. How Learning Plugs Into Current Code

```cpp
// Conceptual — HybridRelevanceMultiplier
float GetValue(Action* action)
{
    if (auto* model = MlModelCache::GetHybridRanker())
    {
        float nn = model->Score(features, action->getName());
        float h  = HeuristicScore(action);          // today’s if/else
        return (1.0f - alpha) * h + alpha * nn;     // alpha ramps 0 → 1
    }
    return HeuristicScore(action);
}
```

Learning updates **weights in the ONNX file**, not C++ constants. Heuristics remain the safety rail.

Same pattern for `PvpPolicyMultiplier` with a second model file (`pvp_policy.onnx`).

---

## 5. One Full Learning Cycle (interrupt head)

1. **Log:** Enemy shaman starts Healing Wave; legal = `{kick, gouge, sinister strike, …}`; bot kicks; 0.4s later cast interrupted → `interrupt_success=1`.
2. **Dataset:** Thousands of such rows, plus cases where bot greed DS and heal landed (`interrupt_success=0`).
3. **Train NN-3:** Input = cast remain_ms, spell embed, my kick CD, partner HP; output = P(kick now).
4. **Validate:** Precision/recall of “should interrupt” on held-out arenas.
5. **Deploy:** Ranker boosts `kick` only when NN-3 says so; heuristic ×1.35 remains if model absent.
6. **Measure:** Kick-on-heal rate ↑, wasted kicks on filler ↓, winrate ↑ → raise `alpha`.

---

## 6. What Does *Not* Learn Live

| Avoid | Why |
|---|---|
| Backprop on map thread | Stalls server; unsafe |
| Random exploration in real arenas | Ruins player experience |
| Per-bot fine-tuning in RAM | Memory + instability at scale |
| Unbounded online RL | Catastrophic actions without mask |

**Allowed “online” behavior:**  
- Write logs  
- Optionally **shadow-mode**: NN proposes, heuristic acts; compare offline  
- A/B: 10% of arena bots load new checkpoint

---

## 7. Human-in-the-Loop (optional but powerful)

- Tag “good” mastered sessions in Discord/tools → higher sample weight  
- GM command: `.bot ml mark good` on a fight → labels the episode  
- Reject known-bad actions (cast while silenced) at dataset build time via `isPossible` replay

---

## 8. Practical Curriculum

| Phase | Learns from | Produces |
|---|---|---|
| **0** | Logging only | Dataset |
| **1** | Imitation on winning mastered arenas | NN-1 ranker beats heuristics on clone accuracy |
| **2** | Interrupt-labeled ticks | NN-3 kick timing |
| **3** | Offline RL + self-play | NN-2 arena policy |
| **4** | Dungeon logs (same interrupt/heal heads) | PvE transfer without new architecture |

---

## 9. Success Metrics

| Metric | Means learning worked |
|---|---|
| Action clone accuracy (top-1 among legal) | Imitation is sane |
| Interrupt precision/recall | Kick head useful |
| Arena winrate vs previous checkpoint | Policy improved |
| Heuristic agreement when NN abstains | Fallback still coherent |
| CPU p99 inference &lt; budget | Still scalable |

---

## Bottom Line

The solution learns like a **flight recorder + flight school**, not like an animal adapting mid-pull:

1. **Play** → log state, legal actions, choice, outcomes  
2. **Train offline** (imitation → ranking → offline RL / self-play) on your 1080  
3. **Export ONNX** into B/C scorers with heuristic blend/fallback  
4. **Repeat** as new logs arrive  

No learning happens inside `DoNextAction` — only inference and logging — which is what keeps thousands of bots and low latency feasible.
