# Stage S2 — Ranker on full legal spellbook

> Design locked in **DEC-026** / [#11](https://github.com/gamesh411/mod-playerbots/issues/11). Execute [#19](https://github.com/gamesh411/mod-playerbots/issues/19). Freeze fields per [#8](https://github.com/gamesh411/mod-playerbots/issues/8) when stage freezes. Movement stays scripted.

| Field | Value |
|-------|--------|
| **Policy** | Multi-logit ranking head + Softmax(τ) / argmax (**DEC-026**); bootstrap = uniform Softmax over legal spell ids |
| **Vocab** | Spell-id template (all ranks + pet); legality mask per tick — frozen **49** warrior / **220** mage |
| **Movement** | Scripted (unchanged) |
| **Policy artifact** | `artifacts/duel/s2/{warrior,mage}.pbml` (expert-off working weights; **not** freeze-cut) |
| **Conf profile** | `duel-farm` with `ActionPolicy=ranker`, `SpellPool=spellbook`; teacher = S1 PBML; full `duel-s2` still [#13](https://github.com/gamesh411/mod-playerbots/issues/13) |
| **Data tag** | `ml_decisions_duel_s2*.csv` (+ `_r2` / `_r3` / `_eo`) |
| **Git tag** | _TBD `stage/s2-…`_ — **do not cut** after first freeze fail |
| **Status** | execute in progress ([#19](https://github.com/gamesh411/mod-playerbots/issues/19)) — runtime + DAgger×2 + expert-off landed; **first stacked freeze FAIL** (near-0% WR both seats). See [handoff 2026-07-29](../research/handoff-2026-07-29-s2-execute-freeze-fail.md) |

## Goal

Expert-style atomic actions — cast any currently legal ability (including mid/low ranks), not only what strategies enumerate.

## Execute progress (2026-07-29)

| Step | Result |
|------|--------|
| Runtime (multi-logit, spellbook, teacher, pet Freeze path) | Landed (`f4d87fe4`+) |
| Accept-duel starve fix (`DUEL_STATE_IN_PROGRESS` gate) | Landed (`cdfd75af`) |
| Bootstrap + DAgger×2 + expert-off farm/train | Done; working PBMLs in `artifacts/duel/s2/` |
| Stacked freeze (δ=0.02, ~2.7k/seat) | **FAIL** — Arms 0.0% / Frost 0.1% vs stock & S1 |
| Pet Freeze (33395) in farm actions | Still ~0 — readiness open |

## Open readiness (DEC-027)

- Movement stays scripted (no kite ML yet).
- Pet Freeze: confirm `DEC-027 Freeze cast OK` in `Playerbots.log` before claiming pet-root combos.
- **Before next freeze attempt:** fix mixed-seat stock path (empty model must not Softmax over spellbook at τ=0) and sanity-check cast distribution / train signal.

## Freeze gate (DEC-026)

Both seats must clear **S1↔S1 + δ** *and* **stock↔stock + δ** (stacked; winrates are non-transitive). Use `eval_duel_winrate.py --baseline-csv … --baseline-csv-s1 …`.
