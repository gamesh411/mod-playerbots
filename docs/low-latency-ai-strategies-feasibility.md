# Low-Latency AI Strategies for Playerbots — Feasibility Evaluation

This document evaluates whether, how, and where modern AI techniques can improve bot decision-making in `mod-playerbots`, with emphasis on **latency**, **server scale**, and **fit with the existing Strategy / Trigger / Action / Value engine**.

It is an analysis only — no implementation is proposed as committed work beyond this document.

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

## 7. Tradeoff Summary

**Possibility:** Yes — especially hybrid local scoring, classical ML, offline LLM authoring, and async LLM chat. End-to-end deep combat AI for *all* bots is not realistic under current scale goals.

**Feasibility constraints that dominate design:**

1. Map-thread sync + thousands of bots → models must be tiny or rare  
2. React delay + GCD → “faster AI” ≠ “faster casts” beyond ~100–500 ms  
3. Existing Strategy interface is a strength — extend Multipliers/Values, don’t bypass them  
4. Content authoring (raids) remains a human/LLM-offline problem more than an inference problem  

**Best overall bet:**

- **Runtime:** Hybrid / classical scorers as Multipliers for mastered & PvP bots  
- **Content:** Offline LLM-assisted strategy generation into CustomStrategy / C++  
- **Social:** Async LLM chat with caching  
- **Foundation:** Keep investing in rule quality — it remains the latency and reliability champion  

---

## 8. Key Code Touchpoints

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

## 9. Conclusion

Low-latency AI strategies are **feasible** if “AI” means **local, budgeted decision helpers** layered on the existing engine — not cloud LLMs on the GCD path. The highest-leverage paths are improving rules, adding classical/hybrid relevance scoring for hard PvP choices, and using LLMs **offline** (and async for chat) to multiply authoring capacity without threatening the project’s defining property: solid performance with thousands of bots.
