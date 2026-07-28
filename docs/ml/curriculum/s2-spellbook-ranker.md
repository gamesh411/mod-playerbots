# Stage S2 — Ranker on full legal spellbook

> Design locked in **DEC-026** / [#11](https://github.com/gamesh411/mod-playerbots/issues/11). Execute [#19](https://github.com/gamesh411/mod-playerbots/issues/19). Freeze fields per [#8](https://github.com/gamesh411/mod-playerbots/issues/8) when stage freezes. Movement stays scripted.

| Field | Value |
|-------|--------|
| **Policy** | Multi-logit ranking head + Softmax(τ) / argmax (**DEC-026**); bootstrap = uniform Softmax over legal spell ids |
| **Vocab** | Spell-id template (all ranks + pet); legality mask per tick |
| **Movement** | Scripted (unchanged) |
| **Policy artifact** | `artifacts/duel/s2/{warrior,mage}.pbml` (after train) |
| **Conf profile** | `duel-farm` with `ActionPolicy=ranker`, `SpellPool=spellbook`; teacher = S1 PBML; full `duel-s2` still [#13](https://github.com/gamesh411/mod-playerbots/issues/13) |
| **Data tag** | `ml_decisions_duel_s2.csv` |
| **Git tag** | _TBD `stage/s2-…`_ |
| **Status** | execute in progress ([#19](https://github.com/gamesh411/mod-playerbots/issues/19)) — runtime + teacher path + bootstrap farm; freeze not cut |

## Goal

Expert-style atomic actions — cast any currently legal ability (including mid/low ranks), not only what strategies enumerate.

## Open readiness (DEC-027)

- Movement stays scripted (no kite ML yet).
- Pet Freeze: `CommandPetCastSpell` + startup `Targets=` dump + per-cast `DEC-027 Freeze cast OK/FAIL` in `Playerbots.log`. Confirm OK lines during farm before claiming pet-root combos on freeze.

## Freeze gate (DEC-026)

Both seats must clear **S1↔S1 + δ** *and* **stock↔stock + δ** (stacked; winrates are non-transitive). Use `eval_duel_winrate.py --baseline-csv … --baseline-csv-s1 …`.
