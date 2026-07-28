# Duel RL showcase curriculum

**Destination:** replayable Arms Warrior vs Frost Mage duel policies — stock Softmax → scripted-vocab ranker → spellbook ranker — with frozen stage artifacts for demos.  
**Map:** [Wayfinder: Duel RL showcase curriculum (S0→S1→S2)](https://github.com/gamesh411/mod-playerbots/issues/4)  
**Branch:** `exp/duel-rl-curriculum` (from upstream `origin/master`)

## Curriculum

| Stage | Policy | Action vocabulary | Stage card | Status |
|-------|--------|-------------------|------------|--------|
| **S0** | Softmax(τ) over **stock** scripted combat relevance | Engine strategy **queue** | [s0-softmax-stock.md](curriculum/s0-softmax-stock.md) | **DEC-022** / [#9](https://github.com/gamesh411/mod-playerbots/issues/9); Engine [#15](https://github.com/gamesh411/mod-playerbots/issues/15) |
| **S1** | Learned **scalar ranker** (Softmax/argmax over `ScoreDuel`) | Same scripted **queue** | [s1-scripted-vocab-ranker.md](curriculum/s1-scripted-vocab-ranker.md) | **DEC-025** / soft-fail **DEC-028** / [#18](https://github.com/gamesh411/mod-playerbots/issues/18) closed |
| **S2** | Learned **multi-logit** ranker (spell-id vocab, all ranks) | Full legal **spellbook** | [s2-spellbook-ranker.md](curriculum/s2-spellbook-ranker.md) | **DEC-026** / [#11](https://github.com/gamesh411/mod-playerbots/issues/11); execute [#19](https://github.com/gamesh411/mod-playerbots/issues/19) |

Roadmap detail: [curriculum/README.md](curriculum/README.md).  
Stage freeze contract (artifacts / conf / tags / cards): [#8](https://github.com/gamesh411/mod-playerbots/issues/8).

**Movement:** scripted Arms chase / Frost kite only for this curriculum (no ML movement).

## Living docs

| Doc | Role |
|------|------|
| [curriculum/](curriculum/) | Stage roadmap + cards |
| [curriculum/sparring-partners.md](curriculum/sparring-partners.md) | Hands-on addclass Arms/Frost sparring (τ=0, no CSV pollution) |
| [DECISIONS.md](DECISIONS.md) | Append-only design log (**DEC-018** = curriculum) |
| [FEATURES.md](FEATURES.md) | Feature packs (70-D state + action flags) |
| [../../tools/ml/README.md](../../tools/ml/README.md) | Train / analyze commands |
| [research/s1-training-lessons-learned.md](research/s1-training-lessons-learned.md) | S1 execute (#18): novel failure modes / debugging intuition |
| [research/handoff-2026-07-28-s1-softfail-s2.md](research/handoff-2026-07-28-s1-softfail-s2.md) | Session handoff: S1 soft-fail (DEC-028) → S2 execute (#19) |
| [research/handoff-2026-07-29-s2-execute-freeze-fail.md](research/handoff-2026-07-29-s2-execute-freeze-fail.md) | S2 runtime + DAgger/expert-off + first stacked freeze FAIL (near-0% WR) |

## Archive

Prior tracks (Mode A primary, pure-random spellbook S0, arena-first farm, latency survey): **[archive/README.md](archive/README.md)**.

## Code map (infrastructure)

| Concern | Location |
|---------|----------|
| Feature vector | `src/Ai/Ml/CombatDecisionFeatures.*` |
| Action flags | `HeuristicScores.h` `ActionFlagIndex` |
| Decision logging | `src/Ai/Ml/MlDecisionLogger.*` |
| Duel bracket | `src/Ai/Ml/MlDuelBracket.*` ([import inventory](research/duel-farm-infrastructure-import.md) / **DEC-021**) |
| Spellbook pool | `src/Ai/Ml/MlDuelSpellPool.*` (S2; ship with import, conf `queue` until S2) |
| Engine policy | `src/Bot/Engine/Engine.cpp` |
| Trainer / analysis | `tools/ml/` |
| Tournament / stage conf | `wotlk-playerbots-server` `scripts/config.ps1` ([#13](https://github.com/gamesh411/mod-playerbots/issues/13)) |

## Constraints

Map-thread latency / no online GD — see [archive/low-latency-ai-strategies-feasibility.md](archive/low-latency-ai-strategies-feasibility.md).
