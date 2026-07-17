# Stage S1 — Ranker on scripted vocabulary

> Design locked in **DEC-025** / [#10](https://github.com/gamesh411/mod-playerbots/issues/10). Freeze fields (artifact paths / git tag) per [#8](https://github.com/gamesh411/mod-playerbots/issues/8) when stage freezes. Fine-grained spellbook / multi-logit choice is **S2** (**DEC-026**), not S1.

| Field | Value |
|-------|--------|
| **Policy** | Per-candidate scalar scores + Softmax(τ) / argmax (**DEC-025**) |
| **Vocab** | Same scripted combat queue as S0 (`SpellPool=queue`) |
| **Movement** | Scripted (unchanged) |
| **τ** | Farm **10**; demo/freeze **≤0** (argmax); shared `MlDuelBracket.SoftmaxTemperature` |
| **Models** | Per-class PBML (warrior / mage; … as pool grows) |
| **Train** | Reward bootstrap on S0 CSV → DAgger×2 (imitate Softmax-stock τ=0) → expert-off (reward) → aggregate retrain |
| **Freeze gate** | Both seats beat stock↔stock baseline winrate by δ (not raw 50%) |
| **Policy artifact** | Bootstrap: `artifacts/duel/s1/warrior.pbml`, `mage.pbml` (reward on S0 CSV; pre-DAgger) |
| **Conf profile** | Deploy keys `MlModelPathDuel.Warrior` / `.Mage`; orchestration `duel-s1` still [#13](https://github.com/gamesh411/mod-playerbots/issues/13) |
| **Data tag** | Bootstrap slice: `ml_decisions_duel_v3.csv` (headerless OK); DAgger needs `duel_v4` + `expert_action` |
| **Git tag** | _TBD `stage/s1-…`_ (after freeze gate) |
| **Status** | execute in progress ([#18](https://github.com/gamesh411/mod-playerbots/issues/18)) — not frozen |

## Goal

Improve Arms and Frost winrates vs Softmax-stock τ=0 within the curated scripted repertoire, then freeze replayable per-class artifacts.
