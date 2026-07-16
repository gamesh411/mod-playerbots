# Stage S0 — Softmax over stock scripted queue

> Design locked in **DEC-022** / [#9](https://github.com/gamesh411/mod-playerbots/issues/9). Freeze fields (artifact paths / git tag) per [#8](https://github.com/gamesh411/mod-playerbots/issues/8) when stage freezes. Engine implementation: follow-on execute ticket on map [#4](https://github.com/gamesh411/mod-playerbots/issues/4).

| Field | Value |
|-------|--------|
| **Policy** | Softmax(τ) over stock Engine combat basket relevance (**DEC-022**) |
| **Vocab** | Scripted strategy queue (not full spellbook) |
| **Movement** | Scripted (Arms charge/reach melee; Frost flee/blink) — Softmax combat-only; empty → stock Peek |
| **τ** | Farm **10**; demo/freeze **0** (argmax); conf `MlDuelBracket.SoftmaxTemperature` |
| **Policy artifact** | _TBD (stock+softmax sentinel / no PBML)_ |
| **Conf profile** | _TBD `duel-s0`_ |
| **Data tag** | _TBD_ |
| **Git tag** | _TBD `stage/s0-…`_ |
| **Status** | not frozen |

## Goal

Reproduce stock playerbot duel decisions with Softmax exploration so later stages improve on a real baseline — not on uniform random.
