# Handoff — S1 soft-fail → S2 execute (2026-07-28)

For a new agent session. Read this first, then the map and DEC-028.

## Where we are

- **Map:** [Wayfinder: Duel RL showcase curriculum (S0→S1→S2)](https://github.com/gamesh411/mod-playerbots/issues/4)
- **S1 execute closed:** [#18](https://github.com/gamesh411/mod-playerbots/issues/18) — **DEC-028 soft-fail** (no `stage/s1` tag)
- **Next frontier ticket:** [Execute S2 spellbook multi-logit ranker (DEC-026)](https://github.com/gamesh411/mod-playerbots/issues/19) — **unblocked**
- **Also open (unrelated to S2 code):** [Orchestration profiles for stage replay](https://github.com/gamesh411/mod-playerbots/issues/13)

Branch: `exp/duel-rl-curriculum` on `gamesh411/mod-playerbots` (fork). Orchestrator: `wotlk-playerbots-server`.

## Destination (unchanged)

Arms Warrior vs Frost Mage duel RL showcase: S0 Softmax-stock → S1 queue ranker → **S2 spellbook multi-logit ranker**, each stage frozen/replayable. Scripted movement only.

## S1 outcome (do not thrash again)

| Round | Arms-ranker | Frost-ranker | Notes |
|-------|-------------|--------------|--------|
| Round-2 (2026-07-17) | 70.9% (−3.7pp) FAIL | 31.0% (+5.2pp) PASS | Best S1; canonical PBML |
| Round-3 (2026-07-28) | 68.9% (−5.7pp) FAIL | 14.2% (−11.6pp) FAIL | Extra v4 + DAgger/expert-off **regressed** |

Stock baseline (v3): Warrior **74.6%** / Mage **25.8%**. Gate δ=**0.02**.

**DEC-028:** soft-fail waiver; pivot to S2; no further S1 expert-off on queue vocab.

Canonical artifacts (round-2):

- `C:\AzerothCore\modules\mod-playerbots\artifacts\duel\s1\warrior.pbml`
- `C:\AzerothCore\modules\mod-playerbots\artifacts\duel\s1\mage.pbml`

Round-3 fails archived as `*.round3-fail-20260728.pbml`.

## Data on disk

| File | Role | Approx size (at stop) |
|------|------|------------------------|
| `C:\AzerothCore-server\ml_decisions_duel_v3.csv` | Softmax-stock baseline / bootstrap (~345 MB) | stock↔stock baseline |
| `C:\AzerothCore-server\ml_decisions_duel_v4.csv` | Ranker farm + `expert_action` (~253 MB, ~1.08M rows) | stopped 2026-07-28 ~21:54 |
| `ml_decisions_duel_mixed_arms_ranker.csv` | Round-3 arms mixed eval | fresh post-retrain |
| `ml_decisions_duel_mixed_frost_ranker.csv` | Round-3 frost mixed eval | fresh post-retrain |

Collection **stopped**; auth/world were stopped after frost eval.

## S2 next (DEC-026 / #19)

Design locked in DEC-026. Execute means roughly:

1. Multi-logit spell-id head + level-80 class template vocab
2. Stop highest-rank collapse for S2; queue Actions expose concrete spell id for DAgger
3. Softmax τ=10/0; DAgger×2 imitate S1 τ=0 (queue Action spell id) → expert-off; no weight warm-start
4. Stacked freeze vs S1↔S1 **and** stock↔stock (both seats)
5. **DEC-027 caveat:** verify **ground-targeted** Water Elemental Frost Nova before claiming that combo

Skills/docs: `docs/ml/*`, especially `DECISIONS.md` (DEC-018…028), `curriculum/s2-spellbook-ranker.md`, `FEATURES.md`, `tools/ml/README.md`. Prefer `/grilling` for new forks; append DECISIONS — never rewrite old DEC entries.

## Ops / sparring (side path, already landed)

Hands-on feel-test without farm pollution:

- Doc: `docs/ml/curriculum/sparring-partners.md`
- Checklist: `wotlk-playerbots-server/scripts/sparring-partners.ps1`
- Engine: τ=0 vs real player; skip duel CSV logging when either participant is real
- **Blocker for addclass:** duel-farm sets `AddClassAccountPoolSize=0` → “no available characters”. Need small pool or `.bot add <Name>` on linked account.
- **Gear:** `autogear bis` is **PvE-only** (`playerbots_bis_gear`). No true PvP-set twin; `DuelFarmGearPreset` `pvp-honor`/`pvp-arena` are ilvl approximations only.

## Repos / paths

| Path | Role |
|------|------|
| `C:\AzerothCore\modules\mod-playerbots` | Module (curriculum branch) |
| `C:\Users\gamesh411\Projects\wotlk-playerbots-server` | Orchestrator scripts |
| `C:\AzerothCore-server` | Runtime + CSVs + conf |
| `C:\AzerothCore-build` | Ninja build |

Daily farm start (when needed):

```powershell
cd C:\Users\gamesh411\Projects\wotlk-playerbots-server\scripts
.\ensure_running.ps1 -ProfileGB 32 -ServerProfile duel-farm -RestartServers
```

Mixed-seat eval:

```powershell
.\ensure_running.ps1 -ProfileGB 32 -ServerProfile duel-farm -DuelMixedSeat arms-ranker -RestartServers
.\ensure_running.ps1 -ProfileGB 32 -ServerProfile duel-farm -DuelMixedSeat frost-ranker -RestartServers
```

## Suggested first move in new chat

**Superseded for current frontier:** use [handoff-2026-07-29-s2-execute-freeze-fail.md](handoff-2026-07-29-s2-execute-freeze-fail.md) (S2 runtime + first freeze FAIL). Historical S1 start checklist kept below.

1. Claim [#19](https://github.com/gamesh411/mod-playerbots/issues/19)
2. Read DEC-026 + `curriculum/s2-spellbook-ranker.md` + Engine/spell-pool current state
3. Chart S2 execute plan (ground-target Nova check early)
4. Do **not** reopen S1 freeze unless destination changes

## Fog still on map

- `duel-s{N}` orchestration profiles vs module conf.dist (#13)
- Showcase metrics dashboard
