# Research: Duel farm infrastructure import

**Ticket:** [Duel farm infrastructure import (bracket, features, logger)](https://github.com/gamesh411/mod-playerbots/issues/12)  
**Map:** [Wayfinder: Duel RL showcase curriculum (S0→S2)](https://github.com/gamesh411/mod-playerbots/issues/4)  
**Sources:** `wip/duel-farm-uncommitted`, `archive/wip/pre-curriculum-2026-07-16`, `exp/duel-rl-curriculum` (= `origin/master` + docs), DEC-012/016–019, `docs/ml/FEATURES.md`

## Question

What subset of existing WIP is required infrastructure for S0–S2 farming, and what should land on `exp/duel-rl-curriculum` vs stay on archive/WIP branches?

## Answer (locked)

**Primary source tree:** tip of `wip/duel-farm-uncommitted` (duel files live only there; archive tip lacks bracket/spellpool).  
**Method:** path-selective checkout / patch onto `exp/duel-rl-curriculum` — **not** a wholesale merge of archive or wip (those carry arena-first and Mode B/C defaults).  
**Docs:** keep curriculum hub already on `exp`; do **not** overwrite with older `docs/ml/*` or `docs/neural-network-*.md` from wip.

### Required — import onto `exp`

| Concern | Paths (from wip tip) | Why |
|---------|----------------------|-----|
| Duel bracket | `src/Ai/Ml/MlDuelBracket.*`, `MlDuelBracketAction.*`, `MlDuelBracketTrigger.*` | DEC-012 farm: pairs, park, rematch, duel start/end |
| 70-D features | `src/Ai/Ml/CombatDecisionFeatures.*` | DEC-017; `ml_decisions_duel_v2+70d` |
| Action flags / scores | `src/Ai/Ml/HeuristicScores.*` | AF_* inputs + logging helpers |
| Decision logger | `src/Ai/Ml/MlDecisionLogger.*` | Duel CSV + terminal via duel end |
| Spellbook pool | `src/Ai/Ml/MlDuelSpellPool.*` | S2 vocab (ship now; conf default stays `queue` until S2) |
| Ranker runtime | `src/Ai/Ml/MlMlpModel.*`, `MlScorer.*` | S1/S2 inference substrate |
| Engine duel hooks | `src/Bot/Engine/Engine.cpp` (duel policy + log calls) | Needed shell; **rewrite** Softmax-stock in [#9](https://github.com/gamesh411/mod-playerbots/issues/9) (wip still implements DEC-013 random) |
| Queue enumerate/pop | `src/Script/WorldThr/Queue.h`, `Queue.cpp` (`Baskets`, `PopBasket`) | Softmax/random over queue candidates |
| Config | `src/PlayerbotAIConfig.*`, `conf/playerbots.conf.dist` (`Ml*` / `MlDuelBracket.*`) | See defaults rewrite below |
| Wiring | `ActionContext.h`, `TriggerContext.h`, `DuelStrategy.cpp`, `AiFactory.cpp` (start-duel rate), `RandomPlayerbotMgr.cpp` (class filter), `RandomPlayerbotFactory.cpp` (**only** duel class-mask / chars-per-account hunks), `Playerbots.cpp` (`OnPlayerDuelStart/End`, logger match-end hook) | Bracket live path |
| Trainer / analysis | `tools/ml/train_ranker.py`, `analyze_duel_ranker.py`, `tools/ml/README.md` | Offline loop for S1/S2 |

### Import dormant (ship code, conf off)

Mode B/C hybrid / PvP-policy strategies are entangled with `StrategyContext` / `AiFactory` / `MlScorer` on the wip tip. Import them **disabled**, do not delete in the first pass:

- `HybridRelevanceStrategy.*`, `PvpPolicyStrategy.*`
- `StrategyContext.h` creators + `AiFactory` addStrategy calls
- Conf: `AiPlayerbot.HybridRelevanceEnabled = 0` (wip default is **true** — must flip), `MlHybridAlpha` / `MlPvpAlpha` stay `0`

These are archived prior-art paths, not curriculum teachers (DEC-018).

### Leave on archive / do not import

| Item | Where | Why |
|------|-------|-----|
| Arena incomplete-team fill | `FillIncompleteRandomArenaTeams` + call sites (`6d05958c`, related factory hunks) | Map out of scope (arena-first); not needed for duel bracket |
| BG / attack-enemy strategy tweaks | `BattlegroundStrategy.cpp`, `AttackEnemyPlayersStrategy.cpp`, related `PlayerbotAI.cpp` bits from archive ML stack | Arena/BG farm track |
| Mixed `PlayerbotFactory.cpp` gear/arena churn from archive ML commits | archive tip | Curriculum branch stays BiS-clean; BiS goes via `fix/bis-*-upstream` |
| Old doc paths | `docs/neural-network-*.md`, wip `docs/ml/DIRECTIONS.md` | Already reorganized under `docs/ml/` on `exp` |
| Wholesale BiS stack | `fix/bis-unlimited-gear-score-limit` | Separate upstream PR line ([#5](https://github.com/gamesh411/mod-playerbots/issues/5)) |

### Conf defaults to rewrite on import (DEC-018)

| Key | Wip tip today | Land on `exp` as |
|-----|---------------|------------------|
| `MlDuelBracket.ActionPolicy` | `random` | `softmax-stock` (or `heuristic` until [#9](https://github.com/gamesh411/mod-playerbots/issues/9) lands the policy string) |
| `MlDuelBracket.SpellPool` | `spellbook` | `queue` |
| `HybridRelevanceEnabled` | `true` | `false` |
| `MlDuelBracket.LogFile` | `ml_decisions_duel_v2.csv` | keep |
| `MlDuelBracket.Pairs` | `1:0-8:2` | keep (Arms vs Frost) |
| `MlDuelBracket.TerminalLambda` | `25` | keep (DEC-016) |

Off-curriculum `random` + `spellbook` remain valid conf values for ablations; they are not stage defaults.

### Rewrite vs import

| Piece | Import as-is? | Follow-on |
|-------|---------------|-----------|
| Bracket, features, logger, spell pool, queue API, factory class filter | Yes | — |
| Engine duel choice | Import shell | [#9](https://github.com/gamesh411/mod-playerbots/issues/9): Softmax(τ) over stock relevance |
| Mode B/C strategies | Import dormant | No curriculum work unless revived |
| Arena fill / BG strategy diffs | **No** | Stay on `archive/wip/pre-curriculum-2026-07-16` |

### Execution

Path-selective apply is a separate AFK task (blocks S0 Engine work). Do not merge `wip/duel-farm-uncommitted` tip wholesale onto `exp`.

## Citations

- Branch topology resolution: [Branch topology: upstream-based exp/duel-rl-curriculum + preserve WIP](https://github.com/gamesh411/mod-playerbots/issues/5)
- DEC-012 / DEC-017 / DEC-018 / DEC-019 in `docs/ml/DECISIONS.md`
- Feature layout: `docs/ml/FEATURES.md` (`ML_INPUT_DIM = 78`)
- Wip vs archive file delta: `git diff --stat archive/wip/pre-curriculum-2026-07-16..wip/duel-farm-uncommitted` (37 files; bracket/spellpool/docs/tools)
- `origin/master` has **no** `src/Ai/Ml/` — curriculum branch must import a foundation, not extend in-tree ML
