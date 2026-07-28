# Stage S1 — Ranker on scripted vocabulary

> Design locked in **DEC-025** / [#10](https://github.com/gamesh411/mod-playerbots/issues/10). **Soft-fail** — no DEC-019 freeze tag (**DEC-028**). Fine-grained spellbook / multi-logit choice is **S2** (**DEC-026** / [#19](https://github.com/gamesh411/mod-playerbots/issues/19)).

| Field | Value |
|-------|--------|
| **Policy** | Per-candidate scalar scores + Softmax(τ) / argmax (**DEC-025**) |
| **Vocab** | Same scripted combat queue as S0 (`SpellPool=queue`) |
| **Movement** | Scripted (unchanged) |
| **τ** | Farm **10**; demo/freeze **≤0** (argmax); shared `MlDuelBracket.SoftmaxTemperature` |
| **Models** | Per-class PBML (warrior / mage); canonical = round-2 weights |
| **Train** | Reward bootstrap → DAgger×2 → expert-off (aggregate CSVs) |
| **Freeze gate** | Mixed seats beat stock↔stock by **δ=0.02** — **not met** |
| **Policy artifact** | `artifacts/duel/s1/warrior.pbml`, `mage.pbml` (round-2; frost PASS / arms FAIL) |
| **Conf profile** | Deploy keys `MlModelPathDuel.Warrior` / `.Mage`; mixed eval via `-DuelMixedSeat`; full `duel-s1` still [#13](https://github.com/gamesh411/mod-playerbots/issues/13) |
| **Data tag** | `ml_decisions_duel_v3.csv` + `ml_decisions_duel_v4.csv` |
| **Git tag** | _none_ (soft-fail; no `stage/s1-…`) |
| **Status** | **soft-fail** ([#18](https://github.com/gamesh411/mod-playerbots/issues/18) closed via **DEC-028**). Pivot to S2 execute. |

## Goal

Improve Arms and Frost winrates vs Softmax-stock τ=0 within the curated scripted repertoire, then freeze replayable per-class artifacts.

## Round-2 snapshot (2026-07-17) — best S1 result

| Seat | WR | vs stock | δ=0.02 |
|------|-----:|---------:|--------|
| Arms-stock ↔ Frost-ranker | Mage **31.0%** (~2.4k) | **+5.2pp** | PASS |
| Arms-ranker ↔ Frost-stock | Warrior **70.9%** (~2.5k) | **−3.7pp** | FAIL |

## Round-3 snapshot (2026-07-28) — final attempt before pivot

Retrain: v3+v4 aggregate, imitate-expert then expert-off, 400k rows/class, 30 epochs.

| Seat | WR | vs stock | δ=0.02 |
|------|-----:|---------:|--------|
| Arms-ranker ↔ Frost-stock | Warrior **68.9%** (~2.5k) | **−5.7pp** | FAIL |
| Arms-stock ↔ Frost-ranker | Mage **14.2%** (~2.4k) | **−11.6pp** | FAIL |

Round-3 weights archived as `*.round3-fail-20260728.pbml`. Canonical deploy stays round-2.
