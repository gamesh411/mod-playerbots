# Stage S1 — Ranker on scripted vocabulary

> Stage card template — filled by [#8](https://github.com/gamesh411/mod-playerbots/issues/8). Implementation: [#10](https://github.com/gamesh411/mod-playerbots/issues/10).

| Field | Value |
|-------|--------|
| **Policy** | Learned ranking head + Softmax/ε |
| **Vocab** | Same scripted combat queue as S0 |
| **Movement** | Scripted (unchanged) |
| **Policy artifact** | _TBD `duel_s1.pbml` + vocab_ |
| **Conf profile** | _TBD_ |
| **Data tag** | _TBD_ |
| **Git tag** | _TBD `stage/s1-…`_ |
| **Status** | not frozen |

## Goal

Beat / differ from stock Softmax within the curated bot action set (novice-to-improved scripted repertoire).
