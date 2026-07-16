# Research: Duel farm infrastructure import

**Ticket:** [Duel farm infrastructure import (bracket, features, logger)](https://github.com/gamesh411/mod-playerbots/issues/12)  
**Map:** [Wayfinder: Duel RL showcase curriculum (S0→S2)](https://github.com/gamesh411/mod-playerbots/issues/4)  
**Sources:** `wip/duel-farm-uncommitted`, `archive/wip/pre-curriculum-2026-07-16`, `exp/duel-rl-curriculum` (= `origin/master` + docs), DEC-012/016–019, **DEC-021** (supersedes DEC-020), `docs/ml/FEATURES.md`

## Question

What subset of existing WIP is required infrastructure for S0–S2 farming, and what should land on `exp/duel-rl-curriculum` vs stay on archive/WIP branches?

## Answer (locked — DEC-021)

**Upstreamability rule:** land **only** code strictly necessary for the duel S0→S2 curriculum. No dormant Mode B/C, no arena-first leftovers “just in case.” Prefer a thin rewrite over shipping unused strategies.

**Primary source tree:** tip of `wip/duel-farm-uncommitted` (duel files live only there; archive tip lacks bracket/spellpool).  
**Method:** path-selective checkout / patch onto `exp/duel-rl-curriculum` — **not** a wholesale merge of archive or wip.  
**Docs:** keep curriculum hub already on `exp`; do **not** overwrite with older `docs/ml/*` or `docs/neural-network-*.md` from wip.

### Required — import onto `exp` (curriculum-only)

| Concern | Paths (from wip tip) | Why |
|---------|----------------------|-----|
| Duel bracket | `src/Ai/Ml/MlDuelBracket.*`, `MlDuelBracketAction.*`, `MlDuelBracketTrigger.*` | DEC-012 farm: pairs, park, rematch, duel start/end |
| 70-D features | `src/Ai/Ml/CombatDecisionFeatures.*` | DEC-017; `ml_decisions_duel_v2+70d` |
| Action flags | `src/Ai/Ml/HeuristicScores.*` — **slim**: keep `FillActionFlags` / `AF_*`; drop or do not wire `Hybrid()` / `PvpPolicy()` teachers | Flags for PBML input + logging; stock Softmax uses Engine relevance, not Mode B heuristics |
| Decision logger | `src/Ai/Ml/MlDecisionLogger.*` — duel CSV + `OnPlayerDuelEnd` path; omit BG/arena match-buffer wiring unless shared code cannot compile without a stub | Duel episodes + terminal (DEC-016) |
| Spellbook pool | `src/Ai/Ml/MlDuelSpellPool.*` | S2 vocab (ship now; conf default stays `queue` until S2) |
| Ranker runtime | `MlMlpModel.*` + **duel-focused** scorer (slim `MlScorer` to one duel model path, or small replacement) — **not** dual hybrid/pvp blend API | S1/S2 inference only |
| Engine duel hooks | `src/Bot/Engine/Engine.cpp` — duel policy + duel log only; **do not** port arena ε-greedy / hybrid blend ticks | Shell for Softmax-stock in [#9](https://github.com/gamesh411/mod-playerbots/issues/9) |
| Queue enumerate/pop | `src/Script/WorldThr/Queue.h`, `Queue.cpp` (`Baskets`, `PopBasket`) | Softmax over queue candidates |
| Config | `PlayerbotAIConfig.*` + `conf/playerbots.conf.dist` — **only** duel bracket + duel logging + single duel model path keys | See defaults below |
| Wiring | `ActionContext.h`, `TriggerContext.h`, `DuelStrategy.cpp`, `AiFactory.cpp` (**only** bracket start-duel rate), `RandomPlayerbotMgr.cpp` (class filter), `RandomPlayerbotFactory.cpp` (**only** duel class-mask / chars-per-account), `Playerbots.cpp` (`OnPlayerDuelStart` / `OnPlayerDuelEnd`) | Bracket live path |
| Trainer / analysis | `tools/ml/train_ranker.py`, `analyze_duel_ranker.py`, `tools/ml/README.md` — duel / class-filter paths | Offline loop for S1/S2 |

### Leave on archive / do not import

| Item | Where | Why |
|------|-------|-----|
| Mode B/C strategies | `HybridRelevanceStrategy.*`, `PvpPolicyStrategy.*` | Not curriculum; contaminates upstream surface |
| Mode B/C wiring | `StrategyContext.h` hybrid/pvp creators; `AiFactory` `hybrid relevance` / `pvp policy` adds; `HybridRelevanceEnabled` | Strict necessity |
| Dual hybrid/pvp scorer blend | `MlScorer` hybrid+pvp alpha blend API as shipped on wip | Replace with duel-only ranker path on import |
| Arena incomplete-team fill | `FillIncompleteRandomArenaTeams` + call sites | Map out of scope |
| BG / attack-enemy strategy tweaks | `BattlegroundStrategy.cpp`, `AttackEnemyPlayersStrategy.cpp`, related `PlayerbotAI.cpp` bits | Arena/BG farm track |
| BG match-end logger hook | `Playerbots.cpp` `OnBattlegroundEnd` → `sMlDecisionLogger.OnMatchEnd` | Not needed for duel farm |
| Arena ε-greedy Engine path | `MlExploreEpsilon` / arena-only explore in Engine | DEC-005 arena prior art; duel uses `ActionPolicy` |
| Mixed `PlayerbotFactory.cpp` gear/arena churn | archive tip | BiS via `fix/bis-*-upstream` |
| Old doc paths | `docs/neural-network-*.md`, wip `docs/ml/DIRECTIONS.md` | Already under `docs/ml/` on `exp` |
| Wholesale BiS stack | `fix/bis-unlimited-gear-score-limit` | Separate upstream PR ([#5](https://github.com/gamesh411/mod-playerbots/issues/5)) |

### Conf defaults on import (DEC-018 + DEC-021)

| Key | Wip tip today | Land on `exp` as |
|-----|---------------|------------------|
| `MlDuelBracket.ActionPolicy` | `random` | `softmax-stock` (or `heuristic` until [#9](https://github.com/gamesh411/mod-playerbots/issues/9)) |
| `MlDuelBracket.SpellPool` | `spellbook` | `queue` |
| `MlDuelBracket.LogFile` | `ml_decisions_duel_v2.csv` | keep |
| `MlDuelBracket.Pairs` | `1:0-8:2` | keep (Arms vs Frost) |
| `MlDuelBracket.TerminalLambda` | `25` | keep (DEC-016) |
| `HybridRelevanceEnabled` / `MlHybridAlpha` / `MlPvpAlpha` / dual model paths | present | **omit** from conf.dist / config structs |
| Duel model path | (hybrid/pvp names on wip) | single curriculum key (e.g. `MlModelPathDuel` or stage-profile path) — name locked at import |

Off-curriculum `random` + `spellbook` may remain as **ActionPolicy / SpellPool enum values** for ablations if cheap; they must not be defaults and must not drag Mode B/C code.

### Rewrite vs import

| Piece | Import as-is? | Follow-on |
|-------|---------------|-----------|
| Bracket, features, spell pool, queue API, factory class filter | Yes (trim non-duel bits) | — |
| Logger / HeuristicScores / MlScorer | Import then **slim** | Drop BG/hybrid surfaces |
| Engine duel choice | Import duel shell only | [#9](https://github.com/gamesh411/mod-playerbots/issues/9): Softmax(τ) over stock relevance |
| Mode B/C, arena fill, BG hooks | **No** | Stay on `archive/wip/pre-curriculum-2026-07-16` / `wip/duel-farm-uncommitted` |

### Execution

[Execute duel farm infrastructure import onto exp/duel-rl-curriculum](https://github.com/gamesh411/mod-playerbots/issues/14) applies this inventory. Do not merge wip tip wholesale onto `exp`.

## Citations

- Branch topology: [Branch topology: upstream-based exp/duel-rl-curriculum + preserve WIP](https://github.com/gamesh411/mod-playerbots/issues/5)
- DEC-012 / DEC-017 / DEC-018 / DEC-019 / DEC-021 in `docs/ml/DECISIONS.md`
- Feature layout: `docs/ml/FEATURES.md` (`ML_INPUT_DIM = 78`)
- Wip vs archive file delta: `git diff --stat archive/wip/pre-curriculum-2026-07-16..wip/duel-farm-uncommitted`
- `origin/master` has **no** `src/Ai/Ml/` — curriculum branch must import a foundation, not extend in-tree ML
