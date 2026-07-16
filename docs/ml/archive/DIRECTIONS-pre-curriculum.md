> **Archived 2026-07** — experiment tracker from the pre-curriculum era.  
> Active roadmap: [`../curriculum/README.md`](../curriculum/README.md).
# ML / PvP AI â€” active directions

Status legend: **active** Â· **paused** Â· **done (baseline)** Â· **next** Â· **superseded**

Update the status column when a track moves. Do not delete rows â€” mark **paused** / **superseded**.

| ID | Direction | Status | Goal | Current artifact | Next concrete step |
|----|-----------|--------|------|------------------|-------------------|
| D1 | Offline reward regression (Mode A) | **done (baseline)** / **superseded as primary by D9** | Predict short (+ terminal) reward; blend via alpha | `pvp_ranker.pbml`, alpha 0.15 | Keep as warm-start / compare; do not extend as target arch |
| D2 | Arena data farm (tournament profile) | **active** | Dense `in_arena` logs, full teams | `arena-tournament` profile, v3 CSV | Keep farming; optional class filter when D12 on |
| D3 | Terminal match backup | **done (baseline)** | `y = short + Î»Â·(+1/âˆ’1)` with `match_id` | Logger v2/v3 schema | Extend to duels via D12 |
| D4 | Îµ-greedy exploration (legal queue actions) | **active (arena)** | Escape pure heuristic support in arenas | `MlExploreEpsilon=0.08` | Unchanged for arena; duels use D13 |
| D5 | Specialist models (arena / BG / PvE) | **active (arena first)** | Separate PBML per zone; merge later if needed | `--arena-only` trainer flag | Class duel specialists after D12 data |
| D6 | **Duels-first PvP skill** | **active** | World-class 1v1 with fine-tuneable difficulty | DEC-007/009/010: **Arms vs Frost**; class-level PBML | Drive via D12 bracket + Warrior packs |
| D7 | Difficulty dial (reaction / knowledge / execution) | **next** | Profiles 1â€“10 gating features + delays | â€” | Spec config knobs after D12 ships |
| D8 | Richer features (CD / DR / class / range) | **done (baseline)** | Orthogonal feature packs beyond 20-D | DEC-017; 70-D state + duel_v2 CSV | Retrain 78-D PBML; optional per-spell CD fractions later |
| D9 | **Action-head / ranking net** | **active (primary arch)** | One forward per tick â†’ scores over legal actions | DEC-011 | Design vocab + trainer ranking loss; keep Mode A baseline |
| D10 | Offline RL / imitation ranking (Mode B/C) | **paused** | True return maximization | â€” | After D9 vocab + D12 episodes |
| D11 | Generalist merge (distill specialists) | **paused** | One brain from arena/BG/PvE experts | â€” | Only after specialists beat heuristics |
| D12 | **Duel bracket (idle â†’ duel)** | **active** | Config pairs, park, pair frequently, duel CSV | DEC-012; engine + `config.ps1` | Ship matcher + population filter; Armsâ†”Frost default |
| D13 | **Duel action policy = legal pool only** | **active** | No heuristic pick in duels; random then ranker | DEC-013 | Keep; pool source = D14 |
| D14 | **Spellbook action pool** | **active** | All castable abilities for emergent learning | DEC-014; `MlDuelSpellPool` | Rebuild; verify duel CSV spell diversity |
| D15 | RNN / temporal memory | **paused** | Hidden state over duel episodes | DEC-015 | After D8 features + D9 ranker plateau |

## Non-goals (for now)

- Online gradient descent on the map thread
- LLM / huge nets in `worldserver`
- Full arena-team strategy before duel mechanics
- Auto-rebuild on every config change
- Capital-city duels (client/server disallow; park outside)
- Using in-code strategy relevance as the duel teacher (DEC-013)
- Training RNN before Markov+features plateau (DEC-015)

## How to use this file

When starting work: pick a **Direction ID**, implement behind one subsystem, add a **DECISIONS.md** entry if the design forks, update **status** here when done.
