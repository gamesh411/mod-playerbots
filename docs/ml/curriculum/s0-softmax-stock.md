# Stage S0 — Softmax over stock scripted queue

> Stage card template — artifact paths / conf keys / git tag filled by [#8](https://github.com/gamesh411/mod-playerbots/issues/8). Implementation: [#9](https://github.com/gamesh411/mod-playerbots/issues/9).

| Field | Value |
|-------|--------|
| **Policy** | Softmax(τ) over stock Engine combat relevance |
| **Vocab** | Scripted strategy queue (not full spellbook) |
| **Movement** | Scripted (Arms charge/reach melee; Frost flee/blink) |
| **Policy artifact** | _TBD (stock+softmax sentinel / no PBML)_ |
| **Conf profile** | _TBD_ |
| **Data tag** | _TBD_ |
| **Git tag** | _TBD `stage/s0-…`_ |
| **Status** | not frozen |

## Goal

Reproduce stock playerbot duel decisions with Softmax exploration so later stages improve on a real baseline — not on uniform random.
