# ML / bot-AI documentation hub

Living docs for the offline learning loop and the PvP skill roadmap.
Keep **directions**, **decisions**, and **features** separate so each stays maintainable.

| Doc | Role |
|-----|------|
| [DIRECTIONS.md](DIRECTIONS.md) | What tracks we are exploring (status, next step, non-goals) |
| [DECISIONS.md](DECISIONS.md) | Locked design choices (append-only; do not rewrite history) |
| [FEATURES.md](FEATURES.md) | Feature/action-flag inventory, orthogonality rules, duel roadmap |
| [../neural-network-learning.md](../neural-network-learning.md) | How learning works (Mode A/B/C) |
| [../neural-network-improvements.md](../neural-network-improvements.md) | Why NN vs pure heuristics (examples) |
| [../low-latency-ai-strategies-feasibility.md](../low-latency-ai-strategies-feasibility.md) | Latency / map-thread constraints |
| [../../tools/ml/README.md](../../tools/ml/README.md) | Train / deploy commands |

## Orthogonality rule (project-wide)

1. **One concern per subsystem** — logging ≠ scoring ≠ training ≠ difficulty dial ≠ content AI (quests/RPG).
2. **Features are additive packs** — shared combat trunk first; class/duel/arena packs later. Never overload one float with two meanings.
3. **Decisions are append-only** — new choice = new DECISIONS entry; supersede old ones explicitly.
4. **Directions track experiments** — a direction can be paused without deleting its decision history.

## Code map (current)

| Concern | Location |
|---------|----------|
| Feature vector (12-D) | `src/Ai/Ml/CombatDecisionFeatures.*` |
| Action flags (8-D) | `HeuristicScores.h` `ActionFlagIndex` |
| Short + terminal logging | `src/Ai/Ml/MlDecisionLogger.*` |
| PBML1 inference | `src/Ai/Ml/MlMlpModel.*`, `MlScorer.*` |
| Heuristic blend multipliers | `HybridRelevanceStrategy.*`, `PvpPolicyStrategy.*` |
| ε-greedy explore | `src/Bot/Engine/Engine.cpp` + `MlExploreEpsilon` |
| Offline trainer | `tools/ml/train_ranker.py` |
| Tournament farm profile | `wotlk-playerbots-server` `scripts/config.ps1` (`arena-tournament`) |
| Duel bracket (pairs / park / login filter) | `src/Ai/Ml/MlDuelBracket.*` + `AiPlayerbot.MlDuelBracket.*` conf |
| Duel action policy (DEC-013) | `Engine.cpp` — legal pool only; `ActionPolicy=random` default |
| Primary arch target | Action-head ranking net (DEC-011 / D9); Mode A = baseline only |
