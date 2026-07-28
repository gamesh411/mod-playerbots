# Curriculum roadmap — S0 → S1 → S2

Showcase learning on **Arms vs Frost** duels. Each stage freezes policy + conf + data tag + stage card + git tag ([#8](https://github.com/gamesh411/mod-playerbots/issues/8)).

| Stage | Policy | Vocab | What it shows |
|-------|--------|-------|----------------|
| [S0](s0-softmax-stock.md) | Softmax(τ) over stock relevance | Scripted combat **queue** | Baseline = real playerbots behaviour + controlled exploration |
| [S1](s1-scripted-vocab-ranker.md) | Learned ranker | Same **queue** | Improves on stock within the bot’s scripted action set |
| [S2](s2-spellbook-ranker.md) | Learned ranker | Full legal **spellbook** | Expert-style atomic abilities (think outside the scripted box) |

**DEC-018** locks this ladder and supersedes DEC-013 / DEC-014 as showcase defaults.

Hands-on feel-test while farming: [sparring-partners.md](sparring-partners.md) (addclass Arms + Frost at a quiet pad).

### Status

| Stage | Implementation ticket | Freeze status |
|-------|----------------------|---------------|
| S0 | [#9](https://github.com/gamesh411/mod-playerbots/issues/9) | not frozen |
| S1 | [#10](https://github.com/gamesh411/mod-playerbots/issues/10) / execute [#18](https://github.com/gamesh411/mod-playerbots/issues/18) | **soft-fail** (**DEC-028**) — no freeze tag; pivot to S2 |
| S2 | [#11](https://github.com/gamesh411/mod-playerbots/issues/11) (design); execute [#19](https://github.com/gamesh411/mod-playerbots/issues/19) | design locked — execute next |

### Out of scope (this curriculum)

- ML movement / positioning (scripted movers remain)
- Pure-uniform-random spellbook as S0 (archived prior art)
- Online training on the worldserver thread
