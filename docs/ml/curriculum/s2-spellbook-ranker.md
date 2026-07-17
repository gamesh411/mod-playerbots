# Stage S2 — Ranker on full legal spellbook

> Design locked in **DEC-026** / [#11](https://github.com/gamesh411/mod-playerbots/issues/11). Freeze fields (artifact paths / git tag) per [#8](https://github.com/gamesh411/mod-playerbots/issues/8) when stage freezes. Movement stays scripted.

| Field | Value |
|-------|--------|
| **Policy** | Multi-logit ranking head + Softmax(τ) / argmax (**DEC-026**) |
| **Vocab** | Fixed per-class **spell-id** template (all ranks, minus noise); legality mask per tick |
| **Movement** | Scripted (unchanged) |
| **Policy artifact** | _TBD per-class PBML + spell-id vocab in manifest_ |
| **Conf profile** | _TBD `duel-s2`_ (`ActionPolicy=ranker`, `SpellPool=spellbook`, demo τ≤0) |
| **Data tag** | _TBD_ |
| **Git tag** | _TBD `stage/s2-…`_ |
| **Status** | design locked — not frozen |

## Goal

Expert-style atomic actions — cast any currently legal ability (including mid/low ranks), not only what strategies enumerate.

## Freeze gate (DEC-026)

Both seats must clear **S1↔S1 + δ** *and* **stock↔stock + δ** (stacked; winrates are non-transitive).
