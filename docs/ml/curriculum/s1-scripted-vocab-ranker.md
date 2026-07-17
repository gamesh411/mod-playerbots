# Stage S1 — Ranker on scripted vocabulary

> Design locked in **DEC-025** / [#10](https://github.com/gamesh411/mod-playerbots/issues/10). Freeze fields (artifact paths / git tag) per [#8](https://github.com/gamesh411/mod-playerbots/issues/8) when stage freezes. Fine-grained spellbook choice is **S2**, not S1.

| Field | Value |
|-------|--------|
| **Policy** | Per-candidate scalar scores + Softmax(τ) / argmax (**DEC-025**) |
| **Vocab** | Same scripted combat queue as S0 (`SpellPool=queue`) |
| **Movement** | Scripted (unchanged) |
| **τ** | Farm **10**; demo/freeze **≤0** (argmax); shared `MlDuelBracket.SoftmaxTemperature` |
| **Models** | Per-class PBML (warrior / mage; … as pool grows) |
| **Train** | Reward bootstrap on S0 CSV → DAgger×2 (imitate Softmax-stock τ=0) → expert-off (reward) → aggregate retrain |
| **Freeze gate** | Both seats beat stock↔stock baseline winrate by δ (not raw 50%) |
| **Policy artifact** | _TBD per-class `.pbml` in `artifacts/duel/s1/`_ |
| **Conf profile** | _TBD `duel-s1`_ (`ActionPolicy=ranker`, `SpellPool=queue`, demo τ≤0) |
| **Data tag** | _TBD_ |
| **Git tag** | _TBD `stage/s1-…`_ |
| **Status** | design locked — not frozen |

## Goal

Improve Arms and Frost winrates vs Softmax-stock τ=0 within the curated scripted repertoire, then freeze replayable per-class artifacts.
