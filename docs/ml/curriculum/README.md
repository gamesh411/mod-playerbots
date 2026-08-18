# Curriculum roadmap — S0 → S1 → S2, then M0 → M1 → M2

Showcase learning on **Arms vs Frost** duels. Each stage freezes policy + conf + data tag + stage card + git tag ([#8](https://github.com/gamesh411/mod-playerbots/issues/8)).

| Stage | Policy | Vocab | What it shows |
|-------|--------|-------|----------------|
| [S0](s0-softmax-stock.md) | Softmax(τ) over stock relevance | Scripted combat **queue** | Baseline = real playerbots behaviour + controlled exploration |
| [S1](s1-scripted-vocab-ranker.md) | Learned ranker | Same **queue** | Improves on stock within the bot’s scripted action set |
| [S2](s2-spellbook-ranker.md) | Learned ranker | Full legal **spellbook** | Expert-style atomic abilities (think outside the scripted box) |
| [M0](m0-movement-substrate.md) | Scripted intent movers on the packet executor | 9-way movement intent | Client-authentic movement substrate (strafe-kite, jump-turn); opens eval epoch 2 |
| [M1](m1-movement-ranker.md) | Learned movement-intent ranker | Same 9-way intent | Learns kiting/chasing from the M0 scripted teacher (DAgger) |
| [M2](m2-ability-coadapt.md) | Co-adapted ability head | Full legal **spellbook** on a movement-active world | Ability policy retrained against learned movement; both seats beat stock |

**DEC-018** locks the S-ladder; **DEC-035/036** add the movement arc (M-track) after the S2 soft-fail pivot.

Hands-on feel-test while farming: [sparring-partners.md](sparring-partners.md) (addclass Arms + Frost at a quiet pad).

### Status

| Stage | Implementation ticket | Freeze status |
|-------|----------------------|---------------|
| S0 | [#9](https://github.com/gamesh411/mod-playerbots/issues/9) | not frozen |
| S1 | [#10](https://github.com/gamesh411/mod-playerbots/issues/10) / execute [#18](https://github.com/gamesh411/mod-playerbots/issues/18) | **soft-fail** (**DEC-028**) — no freeze tag; pivot to S2 |
| S2 | [#11](https://github.com/gamesh411/mod-playerbots/issues/11) / execute [#19](https://github.com/gamesh411/mod-playerbots/issues/19) | **soft-fail** (**DEC-035**) — no freeze tag; movement-first pivot |
| M0 | [#20](https://github.com/gamesh411/mod-playerbots/issues/20) / execute [#21](https://github.com/gamesh411/mod-playerbots/issues/21) | **frozen** (`stage/m0-packet-executor`, DEC-038 gate waiver at 84 %) |
| M1 | [#22](https://github.com/gamesh411/mod-playerbots/issues/22) (design) / execute [#23](https://github.com/gamesh411/mod-playerbots/issues/23) | **frozen** (`stage/m1-movement-ranker`, DEC-041: mage uplift pass +2.3pp, warrior parity waiver) |
| M2 | [#24](https://github.com/gamesh411/mod-playerbots/issues/24) (design) / execute [#28](https://github.com/gamesh411/mod-playerbots/issues/28) | **frozen** (`stage/m2-ability-coadapt`, DEC-051: warrior r1-win +12.1pp, mage r0-bc +20.6pp - first both-seat uplift pass) |

### Out of scope (this curriculum)

- Pure-uniform-random spellbook as S0 (archived prior art)
- Online training on the worldserver thread
