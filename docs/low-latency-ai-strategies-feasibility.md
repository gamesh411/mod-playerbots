# Low-Latency AI Strategies for Playerbots — Feasibility Evaluation

This document evaluates whether, how, and where modern AI techniques can improve bot decision-making in `mod-playerbots`, with emphasis on **latency**, **server scale**, and **fit with the existing Strategy / Trigger / Action / Value engine**.

## Implementation status (PvP-first)

The following are now wired into the combat engine (heuristic scorers; ONNX-ready feature vector):

| Option | Strategy name | Scope | Config |
|---|---|---|---|
| **B** Hybrid relevance | `hybrid relevance` | **Every bot** combat engine | `AiPlayerbot.HybridRelevanceEnabled` (default 1) |
| **C** PvP policy | `pvp policy` | **BG + arena only** | `AiPlayerbot.PvpPolicyEnabled` (default 1) |

Code:

- `src/Ai/Ml/CombatDecisionFeatures.*` — shared feature vector Value
- `src/Ai/Ml/HybridRelevanceStrategy.*` — Option B multiplier
- `src/Ai/Ml/PvpPolicyStrategy.*` — Option C multiplier + PvP triggers
- Factory wiring in `src/Bot/Factory/AiFactory.cpp`

Rule tweaks: arena engages `attack enemy player` on `enemy player near`; world `pvp` strategy relevance raised slightly.

Toggle off either system via `playerbots.conf` if needed for A/B testing.

---

## 1. Context: What We Have Today

Playerbots use a **rule-based GOAP-style engine**:

```
Strategy → Trigger → Action (relevance queue) → Value (blackboard)
```

Core loop (map thread, per bot):

1. `OnPlayerAfterUpdate` → `PlayerbotAI::UpdateAI`
2. Process packets / spell cast yield
3. `Engine::DoNextAction` — evaluate triggers, pick highest-relevance action, execute **one** action
4. `YieldThread(GetReactDelay() + GUID stagger)`

There is **no ML / LLM / ONNX inference** in the tree today. Intelligence is hand-authored C++ (class/raid/dungeon strategies), SQL-driven custom strategies, keyword chat replies, and heuristic gear/travel systems.

### Effective decision cadence (defaults)

| Bot situation | Typical react delay | Notes |
|---|---|---|
| Has real player master | ~100 ms | Fastest path (`ReactDelay`) |
| BG combat (dynamic) | ~250 ms | `FastReactInBG` path |
| World combat, no master | ~500 ms | `base * 5` |
| World, not resting | ~1–3 s | Throughput tradeoff |
| Idle / resting random bots | ~2–20 s | Population filler |
| Inactive alone bots | ~10 s passive | Only relevance ≥ 100 actions |

Hard floors that no smarter policy can beat:

- **Global cooldown** ~500 ms between casts
- **One executed action per engine tick**
- **Synchronous map-thread execution** — blocking work stalls the server
- **Activity throttling** (`BotActiveAlone` ≈ 10%) so thousands of bots stay affordable

**Practical “low latency” for this project** means: *react to combat events within one GCD (~100–300 ms for mastered / BG bots)*, without hurting multi-thousand-bot scale — not sub-millisecond HFT-style latency.

---

## 2. What Problems Are Worth Solving With AI?

| Problem | Latency-sensitive? | Rule engine already strong? | AI upside |
|---|---|---|---|
| Spec rotation (PVE) | Medium | Yes, if maintained | Medium — fewer edge cases |
| Interrupts / dispels / cooldowns | **High** | Yes when triggers exist | Low–medium if rules missing |
| Boss mechanics (raid/dungeon) | High for some | Hand-scripted per encounter | Medium — costly to author |
| Arena / BG micro (kiting, focus) | **High** | Partial (`BattleGroundTactics`) | **High** |
| Party coordination / role adapt | Medium | Partial | High |
| Quest / travel / RPG | Low | TravelMgr + RPG strategies | Medium (planning) |
| Natural chat / social | Low | Keyword replies | **High** (LLM) |
| Strategy authoring speed | Offline | CustomStrategy DB lines | **High** (LLM codegen) |

**Verdict:** Pure chat/LLM and offline strategy generation are easiest wins. Real-time combat AI is feasible only with **tiny, local, non-blocking** models or hybrid scoring that plugs into the existing engine.

---

## 3. Feasible Solution Families

### A. Stay rule-based, invest in faster / denser heuristics

**What:** More triggers, better multipliers, encounter strategies, tighter react delays for party/BG bots, profiling hot values.

**Latency:** Best possible — pure C++ on the map thread, microseconds–low ms per tick.

**Feasibility:** Already proven; this is the current architecture.

| Pros | Cons |
|---|---|
| Zero new infra | Authoring cost scales with content |
| Deterministic, debuggable | Weak at novel / fuzzy situations |
| Scales to thousands of bots | PvP “feel” often plateaus |
| Fits Strategy interface perfectly | Continuous maintenance burden |

**Best for:** Interrupts, raid scripts, known rotations, anything with clear predicates.

---

### B. Hybrid ML relevance / action ranking (recommended combat path)

**What:** Keep Strategy/Trigger/Action. Add a small learned **Multiplier** or action scorer that adjusts relevance from a feature vector (HP%, distances, CD state, auras, target class, etc.). Export model as **ONNX** (or hand-ported weights) and run **in-process** with a hard time budget (e.g. &lt;1 ms).

**Latency:** ~0.1–2 ms inference + existing react delay. Fits inside one tick if models stay small (&lt;~100k params).

**Feasibility:** High. Natural extension point: `Multiplier::GetValue(Action*)` and/or a new `Value` that caches model output.

| Pros | Cons |
|---|---|
| Preserves GOAP safety rails | Needs labeled data or self-play logs |
| Fail-closed to rules if score fails | Feature engineering still required |
| Can improve PvP / priority choice | Model versioning + per-class models |
| Low ops cost (no network) | Hard to explain “why” vs pure rules |

**Possible variants:**

1. **Imitation learning** from logged good players / high-skill bots  
2. **Offline RL** (CQL / BCQ) on logged `(state, action, reward)`  
3. **Supervised priority ranking** (“which of these legal actions next?”)

**Not feasible at scale:** Per-bot fine-tuning online on the worldserver.

---

### C. Tiny neural policies for combat micro (local inference)

**What:** End-to-end or near-end-to-end policy that outputs action IDs / movement intents for arenas or BG skirmishes. Still must call existing `Action::Execute` for legality (LoS, range, GCD, CC).

**Latency:** 1–5 ms CPU for tiny nets; GPU usually unnecessary and awkward in `worldserver`.

**Feasibility:** Medium. Requires a safe action mask, logging pipeline, and strict fallback to rule engine.

| Pros | Cons |
|---|---|
| Best upside for fluid PvP | Training / sim environment is hard |
| Can capture timing hard to hand-code | Risk of illegal / stupid actions |
| Can be limited to mastered bots only | Debugging is painful |
| | Does not replace raid scripting |

**Scope control:** Enable only for `HasRealPlayerMaster()` or arena teams — never for the full random-bot population.

---

### D. Classical ML / utility AI (no deep learning)

**What:** Decision trees, random forests, logistic models, or richer utility curves for target selection, threat, and cooldown spend.

**Latency:** Microseconds–sub-ms.

**Feasibility:** Very high; often enough for “smarter” without neural nets.

| Pros | Cons |
|---|---|
| Easy to ship in C++ | Ceiling lower than deep policies |
| Interpretable (esp. trees) | Still needs features + labels |
| Tiny memory | Less “wow” vs LLM/RL narrative |
| Great fit as Multipliers | |

**Often the sweet spot** between hand rules and deep learning.

---

### E. Async remote inference (LLM APIs, large models)

**What:** Call OpenAI / local vLLM / etc. for chat, quest advice, or high-level plans (“go grind Winterspring”), never for per-GCD combat.

**Latency:** 200 ms–several seconds — **unusable on the map-thread hot path**.

**Feasibility:** High for **non-combat** if designed as:

- Request queued off map thread  
- Result stored in a `Value` / event  
- Next ticks consume cached plan  

| Pros | Cons |
|---|---|
| Excellent natural language | Cost, keys, privacy, rate limits |
| Great for RPG immersion | Cannot drive interrupts / GCD |
| Rapid iteration | Server must tolerate missing replies |
| | Hard dependency / offline servers |

**Hard rule:** Never `await` an HTTP LLM call inside `UpdateAI` / `DoNextAction`.

---

### F. Offline LLM-assisted strategy authoring

**What:** Use LLMs (or humans + LLM) to generate `CustomStrategy` lines, draft encounter triggers, or suggest Action/Trigger code. Humans review; runtime stays rule-based.

**Latency:** Offline — runtime unchanged (still C++ rules).

**Feasibility:** Highest ROI for content velocity.

| Pros | Cons |
|---|---|
| No runtime risk | Generated strategies need QA |
| Works with existing DB custom strategies | Hallucinated spell IDs / logic |
| Speeds raid/dungeon coverage | Doesn’t improve PvP micro alone |
| Fits contributor workflow | |

Example runtime format already exists: `trigger>action!relevance` in `CustomStrategy`.

---

### G. Search / planning (MCTS, HTN) for non-combat

**What:** Short-horizon planners for travel, quest routing, inventory, or dungeon pathing — possibly async.

**Latency:** Combat: usually too slow. Non-combat: acceptable if amortized (seconds).

**Feasibility:** Medium; TravelMgr already approximates this with graph search.

| Pros | Cons |
|---|---|
| Better long-horizon goals | CPU spikes if naive |
| Complements RPG systems | Weak for GCD combat |
| | Overlap with existing TravelMgr |

---

### H. Behavior trees / utility rewrite of the engine

**What:** Replace or wrap GOAP with BT/utility AI frameworks.

**Latency:** Similar to today if kept lean.

**Feasibility:** Low–medium as a project — large rewrite, high regression risk, limited latency win.

| Pros | Cons |
|---|---|
| Modern tooling / clarity | Massive migration |
| May help authoring | Doesn’t magically add intelligence |
| | Years of Strategy content to port |

**Recommendation:** Evolve Multipliers/Values rather than rewrite Engine.

---

## 4. Architecture Patterns That Keep Latency Safe

### Must-haves for any runtime AI

1. **Budgeted inference** — abort and fall back to rules if &gt; N µs  
2. **No network on map thread** — use world-thread / worker queue + `Value` cache  
3. **Action legality via existing Actions** — model proposes; `isPossible`/`isUseful` gate  
4. **Scoped enablement** — mastered bots / arenas first; random bots last  
5. **Deterministic fallback** — if model missing, behavior = today’s strategies  
6. **Feature `Value`s** — compute once per tick, share across scorers  
7. **PerfMonitor hooks** — track `PERF_MON_ACTION`-style timings for inference  

### Suggested integration sketch (hybrid scorer)

```
ProcessTriggers()
PushDefaultActions()
  → for each candidate Action:
       relevance *= RuleMultipliers
       relevance *= MlRelevanceMultiplier(features)   // optional, budgeted
  → pick best → Execute()
```

This preserves prerequisites / continuers / alternatives and emergency relevance (≥90).

### Latency budget sketch (mastered combat bot)

| Stage | Budget |
|---|---|
| Trigger eval + queue | existing (~low ms) |
| ML score (optional) | ≤ 1 ms |
| Action execute (cast/move) | game systems |
| Yield / react delay | ≥ 100 ms (config) |
| GCD | ≥ 500 ms between casts |

Inference is almost never the bottleneck vs react delay + GCD. **Quality and scale matter more than shaving microseconds.**

---

## 5. Comparative Matrix

| Approach | Combat latency fit | Scale (1k+ bots) | Implementation effort | Quality upside | Risk |
|---|---|---|---|---|---|
| A. Better rules | Excellent | Excellent | Low–ongoing | Medium | Low |
| B. Hybrid ML scorer | Excellent | Good if tiny + scoped | Medium | High | Medium |
| C. Tiny neural policy | Good if scoped | Poor if applied globally | High | High (PvP) | High |
| D. Classical ML / utility | Excellent | Excellent | Low–medium | Medium–high | Low |
| E. Remote LLM | Bad (combat) / OK (chat) | Poor if chat floods | Medium | High (social) | Medium–high |
| F. Offline LLM authoring | N/A (offline) | Excellent | Low | High (content) | Low |
| G. Planning (non-combat) | N/A | Medium | Medium | Medium | Medium |
| H. Engine rewrite | Neutral | Unknown | Very high | Unclear | Very high |

---

## 6. Recommended Roadmap (Feasibility Order)

### Phase 0 — Measure (cheap, required)

- Instrument tick time, `DoNextAction` duration, trigger cost, activity fraction  
- Define SLOs: e.g. mastered combat tick p99 &lt; 5 ms CPU; no map stalls  

### Phase 1 — Content & heuristics (always worth doing)

- Close interrupt/dispel gaps with Triggers  
- Improve BG/arena tactics with Multipliers  
- Optionally lower react delay only for party/BG bots near players  

### Phase 2 — Offline AI for authoring

- LLM pipelines that draft `CustomStrategy` / encounter checklists  
- Human review + SQL/C++ merge  

### Phase 3 — Classical / hybrid scoring on mastered bots

- Log `(features, chosen action, outcome)` from good groups  
- Train ranker or shallow model → ONNX → `MlRelevanceMultiplier`  
- Gate behind config; default off  

### Phase 4 — Scoped PvP policies (optional)

- Arena-only tiny policy with hard action mask  
- Never enable for full random-bot population without proven CPU budget  

### Explicitly defer / avoid

- LLM calls inside combat ticks  
- Large transformers in-process on `worldserver`  
- Online RL exploring on live players without sandbox  
- Full Engine rewrite for “AI”

---

## 7. Hardware Cost Baseline (Your Machine)

Estimates below target a typical **Intel Core i7-8700-class** 8th-gen CPU (6C/12T, 3.2 GHz base, up to ~4.6 GHz single-core turbo) and an **NVIDIA GeForce GTX 1080** (Pascal, ~8.9 TFLOPS FP32, PCIe 3.0).

These are **engineering estimates**, not measured benchmarks on this repo. Treat them as order-of-magnitude planning numbers (±2–3× depending on cache locality, contention with `worldserver`, and implementation quality).

### Conversion helpers

| Quantity | Approx. value |
|---|---|
| Useful single-thread clock under mixed load | ~4.0 GHz |
| Cycles in 1 µs | ~4,000 |
| Cycles in 1 ms | ~4,000,000 |
| Cycles in 100 ms (`ReactDelay`) | ~400,000,000 |
| Map-thread “safe” AI budget per mastered bot tick | **≤ ~1–2 ms CPU** (4–8M cycles) preferred; hard ceiling before players feel lag is closer to several ms if many bots share the core |

### Critical GPU reality check (GTX 1080)

For the **tiny** models that fit combat ticks, the 1080 is usually **slower wall-clock than the CPU**:

| Overhead | Typical cost on GTX 1080 |
|---|---|
| CUDA context / first-call warmup | milliseconds–tens of ms (one-time / rare) |
| Kernel launch + CPU↔GPU sync | ~50–300+ µs |
| PCIe round-trip of a small feature vector | tens of µs |

**Rule of thumb:** if CPU inference finishes in **&lt; ~200 µs**, keep it on CPU. Use the 1080 only for **batched / large** work (vision, large chat embeddings, offline training) — not per-bot GCD decisions.

### Scale arithmetic (why costs matter)

Assume 200 mastered/party bots actively deciding every ~100–250 ms, plus thousands of random bots on slower cadences.

| Per-bot AI cost | 200 bots @ 10 Hz | Extra CPU load (rough) |
|---|---|---|
| 10 µs | 20 ms/s | ~0.3% of one 4 GHz core |
| 100 µs | 200 ms/s | ~3% of one core |
| 1 ms | 2.0 s/s | **~50% of one core** (painful) |
| 5 ms | 10 s/s | **>2 cores saturated** (not viable on map thread) |

---

## 8. In-Game Examples, Outcomes, and Cost per Approach

Outcome scale used below:

- **Best:** skilled human-adjacent feel in that niche  
- **Average:** noticeable but imperfect improvement over today’s bots  
- **Minimum added value:** smallest improvement still worth shipping  

Costs are **per decision** unless noted.

---

### A. Better rules / denser heuristics

#### In-game examples
1. **ICC Sindragosa** — air phase: bots already stacked correctly because a raid trigger forces position before the next GCD.  
2. **Arena** — resto shaman kicks the healer’s cast within ~100–200 ms because an `interrupt` trigger fires on enemy casting + interrupt ready.  
3. **Dungeon tank** — blood DK holds threat on trash packs via pull/AoE priority multipliers instead of single-target tunneling.

#### Outcomes
| Tier | What players see |
|---|---|
| **Best** | Scripted raid/dungeon bots that look intentionally programmed; interrupts feel “on time”; wipe rate approaches a practiced pug |
| **Average** | Fewer obvious mistakes on known encounters; still fail novel edge cases (unexpected pull size, weird LOS) |
| **Minimum** | One missing kick/dispel path closed; one boss mechanic no longer griefs the group |

#### Cost (runtime)
| Platform | Cycles / time |
|---|---|
| **i7-8700** | ~2k–50k cycles per trigger/multiplier eval → **~0.5–12 µs**; full `DoNextAction` with ~tens of triggers typically **~20–200 µs** today-class code |
| **GTX 1080** | N/A — pure C++ rules stay on CPU |

#### Capability & cost assessment
- **Capability:** Highest reliability for *known* predicates; weak at fuzzy judgment.  
- **Cost to build:** Dev time, not CPU — hours to days per mechanic.  
- **Runtime cost:** Negligible vs GCD/react delay.  
- **Scales to thousands of bots:** Yes.  
- **Verdict:** Always do this first for interrupt/raid gaps.

---

### B. Hybrid ML relevance / action ranking (local ONNX)

#### In-game examples
1. **2v2 arena** — given {enemy casting Poly, partner HP 35%, trinket CD, kick CD}, model boosts `kick` / `cloak` relevance over filler DPS.  
2. **Healer triage** — when 3 allies are low, ranker prefers the tank over a DPS with a defensive available.  
3. **Warrior CD spend** — avoid dumping Recklessness on a target about to immune / vanish based on learned patterns.

#### Outcomes
| Tier | What players see |
|---|---|
| **Best** | Bots make “priority call” decisions close to a decent rated player in common arenas; fewer grief CD dumps |
| **Average** | Rotations/priorities improve ~10–30% in messy fights; still lose to coordinated humans |
| **Minimum** | Slightly better target/heal priority; occasional correct clutch kick that rules missed |

#### Model size assumption
MLP or small gradient-boosted trees / distilled net: **~32–128 input features**, **~10k–100k parameters**, outputs relevance deltas for ~20–80 legal actions.

#### Cost (runtime, per bot tick)
| Platform | Estimate |
|---|---|
| **i7-8700** | Feature gather 5–30 µs + inference **10–80 µs** → **~15–110 µs** (~60k–440k cycles). Budgeted hard cap: abort at 1 ms. |
| **GTX 1080** | Transfer+launch often **80–300 µs** alone → **usually slower than CPU** for this size. Only useful if batching **dozens+** bots asynchronously (awkward with map-thread design). |

#### Capability & cost assessment
- **Capability:** Strong at *choosing among legal actions*; cannot invent new Actions.  
- **Training cost:** Logging pipeline + offline train (GPU helpful here: hours on 1080).  
- **Runtime cost:** Comfortable for mastered bots; enable carefully for BG groups; **do not** enable for entire random-bot population without proof.  
- **At 200 bots × 10 Hz × 50 µs:** ~100 ms CPU/s ≈ **2.5% of one core** — viable.  
- **Verdict:** Best runtime AI bet for combat quality vs cost.

---

### C. Tiny neural policy for combat micro (local)

#### In-game examples
1. **Rogue vs mage** — weave between kick windows, re-stealth angles, and kidney timing without a giant if/else tree.  
2. **Hunter kiting** — continuously adjust strafe + disengage distance vs warrior charge range.  
3. **Flag carrier juke** in WSG — feint pathing that pure waypoint tactics miss.

#### Outcomes
| Tier | What players see |
|---|---|
| **Best** | Arena partner that feels “alive” in movement and trading; can win some games vs average humans |
| **Average** | Better kiting/peeling than today’s BG tactics; still telegraphed vs good players |
| **Minimum** | Slightly less stuck-in-melee / slightly better spacing |

#### Model size assumption
Policy net **~100k–1M params** (still “tiny”), possibly with a small observation stack; must output into existing Action mask.

#### Cost (runtime, per decision)
| Platform | Estimate |
|---|---|
| **i7-8700** | **0.2–2.0 ms** (0.8M–8M cycles) depending on size/features; 1M-param MLP often ~0.3–1.0 ms single-thread |
| **GTX 1080** | Compute can be &lt;0.1–0.5 ms, but **end-to-end often 0.3–1.5 ms** after sync; wins only if model is large enough *and* batched |

#### Capability & cost assessment
- **Capability:** Highest upside for fluid PvP; fragile without hard legality gates.  
- **Risk cost:** Illegal casts, spinning, tunnel vision — needs shadow mode + fallback.  
- **At 40 arena bots × 10 Hz × 1 ms:** 400 ms/s ≈ **10% of one core** — OK if scoped.  
- **At 2000 random bots × even 2 Hz × 1 ms:** 4.0 s/s → **~1 full core** — **not acceptable**.  
- **Verdict:** Feasible only as opt-in for mastered/arena bots.

---

### D. Classical ML / richer utility AI (no deep learning)

#### In-game examples
1. **Target selection** — logistic/utility score: “kill healer unless they have Bubble and a softer kill exists.”  
2. **Loot/quest RPG** — decision tree picks vendor vs trainer vs grind node from local state.  
3. **Threat / taunt** — forest predicts wipe risk from threat deltas and pre-taunts.

#### Outcomes
| Tier | What players see |
|---|---|
| **Best** | Clearly smarter focus fire / heal priority without “neural” branding |
| **Average** | Fewer dumb targets; still lose complex swaps |
| **Minimum** | One better default target heuristic |

#### Cost (runtime)
| Platform | Estimate |
|---|---|
| **i7-8700** | Decision tree depth 10–20: **~1–10 µs**; random forest 50 trees: **~20–100 µs**; hand utility curves: **&lt;5 µs** |
| **GTX 1080** | Not useful |

#### Capability & cost assessment
- **Capability:** Excellent mid-tier intelligence; easy to debug with feature importances.  
- **Cost:** Lowest ML ops burden; can even hardcode trained weights as C++.  
- **Scale:** Fine for large bot counts if kept &lt;50 µs.  
- **Verdict:** Often captures 70% of hybrid-ML value at 20% of complexity.

---

### E. Async remote / local LLM (chat & high-level plans)

#### In-game examples
1. Player whispers “*need water*” → bot replies naturally and trades water (not keyword `REPLY_NOT_UNDERSTAND`).  
2. Party chat “*stack for blizz*” → bots acknowledge and the combat engine still uses raid strategies for actual stacking.  
3. Random bot RPG — LLM proposes “I’m heading to Storm Peaks for quests”; TravelMgr executes the route next ticks.

#### Outcomes
| Tier | What players see |
|---|---|
| **Best** | Immersive social world; bots feel like players in chat; plans sound intentional |
| **Average** | Fun chatter with occasional nonsense; combat unchanged |
| **Minimum** | Better greeting/quest-help replies than today’s string matching |

#### Cost (runtime) — **not for combat ticks**
| Path | Latency | CPU/GPU on your box |
|---|---|---|
| Cloud API (GPT-class) | **300 ms–3 s** network+queue | Near-zero local CPU; $ cost per 1k msgs |
| Local 7B LLM (quantized) on **GTX 1080 8 GB** | **~30–150 ms/token** typical → **1–8+ s/reply** | Saturates GPU; VRAM tight |
| Local 7B on **i7-8700** alone | Often **&gt;100 ms/token** → multi-second replies | Multi-core thrash; bad beside `worldserver` |
| Tiny local reply model (≤100M params) on CPU | **5–50 ms** per short reply | Acceptable **async only** |

Cycle framing: a 2 s local LLM reply ≈ **8e9 cycles** of wall time on one busy core — fine async, **catastrophic** if called from `DoNextAction`.

#### Capability & cost assessment
- **Capability:** Best for language/social; **zero** combat latency value.  
- **Must** use worker queue + cached `Value`.  
- **1080 sweet spot:** offline training / batched embeddings / optional local chat if VRAM reserved and rate-limited.  
- **Verdict:** High immersion ROI; strict architectural quarantine from combat.

---

### F. Offline LLM-assisted strategy authoring

#### In-game examples
1. Generate a first draft `CustomStrategy` for **Heroic Halls of Reflection** trash interrupts.  
2. Draft ICC Lady Deathwhisper phase add priorities as trigger→action lines.  
3. Suggest missing mage counterspell conditions from a boss ability list.

#### Outcomes
| Tier | What players see |
|---|---|
| **Best** | New dungeon/raid coverage lands weeks faster; bots clear content that lacked scripts |
| **Average** | Drafts need fixups but still cut authoring time ~2–5× |
| **Minimum** | Checklist of triggers a human implements faster |

#### Cost
| Platform | Estimate |
|---|---|
| **Runtime in game** | **0** — ships as normal C++/SQL rules (~µs as approach A) |
| **Authoring on i7-8700 / 1080** | Cloud LLM: seconds per draft; local 7B on 1080: slower but workable offline |
| **Human QA** | Still required — main real cost |

#### Capability & cost assessment
- **Capability:** Multiplies content velocity; does not improve PvP micro by itself.  
- **Risk:** Hallucinated spell names/IDs — gate with validation.  
- **Verdict:** Highest leverage per engineering hour for PvE coverage.

---

### G. Search / planning (MCTS, HTN) for non-combat

#### In-game examples
1. Quest hub routing — pick the next 5 quest objectives with least travel time.  
2. Bag / repair / AH trip planning before a grind session.  
3. Dungeon meeting stone → instance → boss order pre-plan (execution still rule-driven).

#### Outcomes
| Tier | What players see |
|---|---|
| **Best** | Random bots look purposeful instead of wandering; fewer stuck/idle loops |
| **Average** | Slightly smarter travel/quest order |
| **Minimum** | One less pointless zone hop |

#### Cost (runtime)
| Platform | Estimate |
|---|---|
| **i7-8700** | Light Dijkstra on TravelMgr-scale graph: **~50 µs–2 ms**; naive MCTS combat: **10–100+ ms** (too slow) |
| **GTX 1080** | Rarely helps graph search; optional for heavy batch planning offline |

#### Capability & cost assessment
- **Capability:** Good for minutes-scale goals; poor for GCD fights.  
- **Overlap:** TravelMgr already covers much of this.  
- **Verdict:** Incremental improvements to travel/RPG; avoid MCTS in combat.

---

### H. Behavior-tree / utility engine rewrite

#### In-game examples
Same situations as today (raids, grind, follow) — but authored as BT nodes instead of Strategy/Trigger graphs. Players may not notice anything unless content quality improves simultaneously.

#### Outcomes
| Tier | What players see |
|---|---|
| **Best** | Cleaner contributor DX → faster content long-term |
| **Average** | Months of parity bugs while old strategies are ported |
| **Minimum** | No player-visible gain |

#### Cost
| Platform | Estimate |
|---|---|
| **i7-8700 runtime** | Similar to A if well written (**tens–hundreds µs**) |
| **Project cost** | Extremely high — rewrite + port of class/raid/dungeon strategies |
| **GTX 1080** | Irrelevant |

#### Capability & cost assessment
- **Capability:** Organizational, not intelligence.  
- **Verdict:** Do not pursue for “AI latency”; evolve Multipliers/Values instead.

---

## 9. Side-by-Side Cost & Value Card

| Approach | In-game win condition | Min value | Avg value | Best value | i7-8700 / decision | GTX 1080 / decision | Scale safety |
|---|---|---|---|---|---|---|---|
| **A Rules** | Correct kick / boss script | 1 mechanic fixed | Fewer wipes on known bosses | Near-scripted clear | **0.5–12 µs** (eval) / **20–200 µs** tick | N/A | Excellent |
| **B Hybrid ML** | Smarter action priority | Slightly better triage | Noticeably better arena/heal calls | Near good-player priorities | **15–110 µs** | Usually worse (overhead) | Good if scoped |
| **C Tiny policy** | Fluid PvP micro | Better spacing | Stronger arena bots | Scary-good duelist feel | **0.2–2 ms** | ~0.3–1.5 ms e2e | Poor if global |
| **D Classical ML** | Smarter focus/utility | Better default target | Clear priority gains | “Smart” without NN | **1–100 µs** | N/A | Excellent |
| **E LLM async** | Natural chat / plans | Better replies | Fun immersion | Believable social bots | **ms–s** (async only) | Local 7B: **s/reply** | OK if rate-limited |
| **F Offline LLM** | Faster content authoring | Better checklist | 2–5× author speed | Rapid raid coverage | **0 in combat** | Offline only | Excellent |
| **G Planning** | Purposeful travel | Fewer idle hops | Smarter questing | “Has a schedule” bots | **0.05–2 ms** | Rarely useful | Medium |
| **H Rewrite** | Dev ergonomics | None | Parity churn | Long-term DX | ~same as A | N/A | Unknown |

### Where your 1080 actually helps

| Workload | 1080 usefulness |
|---|---|
| Per-bot combat inference (&lt;100k params) | **Poor** — CPU wins |
| Offline training of rankers/policies | **Good** |
| Local LLM chat (7B) | **Marginal** — slow, VRAM-tight, must be async + rare |
| Embedding batches / offline eval | **Good** |
| Live map-thread decisions | **Avoid** |

### Where your i7-8700 budget should go

| Priority | Spend cycles on |
|---|---|
| 1 | Existing Engine + better rules (A) |
| 2 | Classical / hybrid scorers ≤ ~100 µs (D/B) |
| 3 | Async queues for chat/plans (E) |
| 4 | Optional arena policy ≤ ~1 ms, few bots (C) |

---

## 10. Tradeoff Summary

**Possibility:** Yes — especially hybrid local scoring, classical ML, offline LLM authoring, and async LLM chat. End-to-end deep combat AI for *all* bots is not realistic under current scale goals.

**Feasibility constraints that dominate design:**

1. Map-thread sync + thousands of bots → models must be tiny or rare  
2. React delay + GCD → “faster AI” ≠ “faster casts” beyond ~100–500 ms  
3. Existing Strategy interface is a strength — extend Multipliers/Values, don’t bypass them  
4. Content authoring (raids) remains a human/LLM-offline problem more than an inference problem  
5. On an **i7-8700 + GTX 1080**, combat AI should be **CPU-first**; the 1080 is an **offline training / optional async** device, not a per-GCD accelerator  

**Best overall bet:**

- **Runtime:** Hybrid / classical scorers as Multipliers for mastered & PvP bots (**~15–100 µs** on your CPU)  
- **Content:** Offline LLM-assisted strategy generation into CustomStrategy / C++  
- **Social:** Async LLM chat with caching  
- **Foundation:** Keep investing in rule quality — it remains the latency and reliability champion  

---

## 11. Key Code Touchpoints

| Area | Path |
|---|---|
| Engine tick | `src/Bot/Engine/Engine.cpp` (`DoNextAction`) |
| React delay | `src/Bot/PlayerbotAI.cpp` (`GetReactDelay`) |
| Strategy API | `src/Bot/Engine/Strategy/Strategy.h` |
| Multipliers | `src/Bot/Engine/Multiplier.h` (+ raid/dungeon multipliers) |
| Custom DB strategies | `src/Bot/Engine/Strategy/CustomStrategy.cpp` |
| Activity throttle | `src/Bot/PlayerbotAI.cpp` (`AllowActivity`) |
| Config knobs | `conf/playerbots.conf.dist` (`ReactDelay`, `IterationsPerTick`, `BotActiveAlone`) |
| BG tactics | `src/Ai/Base/Actions/BattleGroundTactics.cpp` |
| Perf hooks | `src/Bot/Debug/PerfMonitor.h` |

---

## 12. Conclusion

Low-latency AI strategies are **feasible** if “AI” means **local, budgeted decision helpers** layered on the existing engine — not cloud LLMs on the GCD path. On your hardware, expect rule/classical/hybrid decisions in **microseconds to ~0.1 ms**, scoped PvP policies up to **~1–2 ms**, and LLMs only **asynchronously**. The highest-leverage paths remain: better rules, ≤100 µs scorers for hard priorities, offline LLM authoring for PvE coverage, and async chat — preserving solid performance with thousands of bots.
