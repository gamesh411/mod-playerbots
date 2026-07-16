# Stage S2 — Ranker on full legal spellbook

> Stage card template — filled by [#8](https://github.com/gamesh411/mod-playerbots/issues/8). Implementation: [#11](https://github.com/gamesh411/mod-playerbots/issues/11).

| Field | Value |
|-------|--------|
| **Policy** | Learned ranking head + Softmax/ε |
| **Vocab** | Full legal spellbook (`MlDuelSpellPool`) |
| **Movement** | Scripted (unchanged) |
| **Policy artifact** | _TBD `duel_s2.pbml` + spell vocab_ |
| **Conf profile** | _TBD_ |
| **Data tag** | _TBD_ |
| **Git tag** | _TBD `stage/s2-…`_ |
| **Status** | not frozen |

## Goal

Expert-style atomic actions — cast any currently legal ability, not only what strategies enumerate.
