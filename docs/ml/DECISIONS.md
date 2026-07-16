# ML / PvP AI — decision log

Append-only. Newest at the bottom. To reverse a choice, add a new entry that **supersedes** an older ID.

Format:

```
### DEC-NNN — YYYY-MM-DD — title
Status: accepted | superseded by DEC-XXX
Context: …
Decision: …
Why: …
Consequences: …
```

---

### DEC-001 — 2026-07-13 — Hybrid short + terminal rewards
**Status:** accepted  
**Context:** Short-horizon (~2s) reward alone biases local trades; win/loss ignored.  
**Decision:** Train on `reward = short_reward + λ * terminal` with `terminal ∈ {+1,−1,0}`; keep short reward for dense shaping.  
**Why:** Need both micro-execution and match outcome.  
**Consequences:** Logger buffers arena/BG rows until `OnMatchEnd`; open-world writes immediately with `terminal=0`.

### DEC-002 — 2026-07-13 — Specialists first, then optional merge
**Status:** accepted  
**Context:** Mixed PvE/PvP logs wash out arena policy.  
**Decision:** Train separate models by activity zone; first specialist = unified rated arena (2v2+3v3 together). Merge/distill only later.  
**Why:** Cleaner gradients; matches tournament farm; easy A/B.  
**Consequences:** `--arena-only` / `--pvp-only` / `--pve-only` in trainer; deploy `pvp_ranker.pbml` for arena.

### DEC-003 — 2026-07-13 — Match-id backup onto all steps
**Status:** accepted  
**Context:** Need terminal signal on every decision in a match.  
**Decision:** Assign `match_id` (BG instance id); on match end apply terminal to all buffered steps for that bot.  
**Why:** Stronger credit than end-row-only; still simple offline.  
**Consequences:** CSV columns `match_id`, `short_reward`, `terminal`; log file rotated when schema changes (`v2`, `v3`, …).

### DEC-004 — 2026-07-13 — Baseline train then logger (two-track)
**Status:** accepted  
**Context:** Farm already producing short-only arena rows.  
**Decision:** Train short-only baseline at low alpha; ship match_id logger next; compare later.  
**Why:** Do not block learning on infrastructure.  
**Consequences:** `pvp_ranker.pbml` from older CSV; new data in `ml_decisions_v2+`.

### DEC-005 — 2026-07-13 — ε-greedy on legal combat queue actions
**Status:** accepted  
**Context:** Pure heuristic policy never tries off-script tactics.  
**Decision:** With probability ε, pick a random useful/possible *loggable combat* action from the Engine queue (arena-only by default). Log `explored`.  
**Why:** Bounded exploration without illegal spam.  
**Consequences:** `MlExploreEpsilon`, `MlExploreArenaOnly`; new log `ml_decisions_v3.csv`.

### DEC-006 — 2026-07-13 — Low alpha while exploring
**Status:** accepted  
**Context:** High alpha on heuristic-only data can mode-collapse.  
**Decision:** Keep MLP alpha ~0.15 until explore+terminal data is thick; raise only after retrain curriculum.  
**Why:** Warm start without amplifying narrow policy.  
**Consequences:** Tournament profile sets alpha 0.15.

### DEC-007 — 2026-07-13 — Duels-first skill vertical
**Status:** accepted  
**Context:** Want world-class PvP and fine-tuneable difficulty; arena/BG also in scope later.  
**Decision:** Optimize **1v1 duels first** (mechanics, CDs, DR), then reuse feature packs in arena/BG with different difficulty masks.  
**Why:** Cleanest win/loss; less confounding than team arenas; fastest feedback for difficulty dial.  
**Consequences:** Direction D6 primary; arena farm (D2) remains data pipeline, not the skill-definition target.

### DEC-009 — 2026-07-13 — First duel lab: Arms Warrior vs Frost Mage
**Status:** accepted (amended specs)  
**Context:** Duels-first vertical (DEC-007); need a concrete matchup.  
**Decision:** Start with **Arms Warrior vs Frost Mage** as the duel lab. Implement richer packs / difficulty on **Warrior first**; Mage as opponent (then swap).  
**Why:** Forces kick/DR/walls/roots/range; classic control Mage vs Arms pressure.  
**Consequences:** Direction D6 scoped to this matchup before other classes; Fire Mage foil later.

### DEC-010 — 2026-07-13 — Model granularity for duels: class specialists, not per-spec yet
**Status:** accepted  
**Context:** Whether to train per class, per spec, or one generic duelist that “detects spec.”  
**Decision:** **Class-level specialists** for the MLP (`duel_warrior.pbml`, later `duel_mage.pbml`). Spec is an **input feature / difficulty pack**, not a separate model in v1. Do **not** rely on one generic net to infer spec from weak signals.  
**Why:** Spec changes action legality and timings (Arms vs Fury, Fire vs Frost) but splitting per-spec halves data too early; a single generic model blurs matchup priors. Class packs keep data pooled within Warrior while still allowing Arms/Fury flags.  
**Consequences:** Trainer gets `--class` / class filter later; FEATURES gain optional `spec_*` bits inside a Class pack; per-spec PBML only if class model plateaus on systematic Arms-vs-Fury failures.  
**Clarification:** A class model is **not** one-net-per-matchup. `duel_warrior.pbml` is “how Warrior chooses among *Warrior* actions” in whatever duel/arena state it sees — including vs Mage, Rogue, etc. Opponent class/spec should be **features** (when added), not separate Warrior-vs-Frost / Warrior-vs-Fire files in v1. Matchup-specific PBML only if needed after a strong class baseline.

### DEC-011 — 2026-07-13 — Action-head / ranking net (supersedes Mode A as primary)
**Status:** accepted  
**Context:** Per-action reward regression (state+action_flags → scalar) scores candidates one-by-one; ranking head scores all legal actions in one forward.  
**Decision:** Primary learning target is an **action-head / ranking net**: state features → logits/scores over the legal action vocabulary in **one forward**; pick argmax (or ε-greedy over softmax). Mode A reward regression remains a **baseline / warm-start** artifact only.  
**Why:** Matches how the Engine already ranks a queue; better credit for “which action among these”; cleaner migrate path to imitation / offline RL.  
**Consequences:** Trainer grows a ranking objective; PBML schema will gain `action_vocab` / multi-logit output; Engine inference path changes after duel data is thick. Direction **D9** becomes active; **D1** stays “done (baseline)” but not the target architecture.

### DEC-012 — 2026-07-13 — Duel bracket while waiting for arenas
**Status:** accepted  
**Context:** Q4 data generation for duels-first; want dense 1v1 logs without idle bots. Capitals disallow duels (`AREA_FLAG_ALLOW_DUELS`).  
**Decision:** Configurable **duel bracket** runs in parallel with arena queueing:
- **Pairs** are config lists of `class:specTab` vs `class:specTab` (multiple pairs allowed; default Arms Warrior `1:0` vs Frost Mage `8:2`).
- Eligible bots **duel while waiting** (including while **arena-queued**, not while inside arena/BG instance).
- **Population** creates/logins only classes present in the pair list; specs forced via `RandomClassSpecProb` so idle non-matching specs do not exist.
- **Park** bots on duel-allowed pads (Elwynn / Durotar outskirts), **not** capital banker teleports, when bracket is on.
- Decisions go to a **separate duel logfile** (`MlDuelBracket.LogFile`); terminal win/loss via `OnPlayerDuelEnd`.
- Matcher is a global waitlist (teleport-together) so pairing is frequent, not proximity RPG spam.
**Why:** Minimizes idle; maximizes Arms↔Frost (etc.) episodes; keeps arena farm optional on the same bots.  
**Consequences:** Under bracket mode, arena comps are whatever classes are in the pair list (v1 = Warrior+Mage only). Expand pairs when healers/other matchups are needed. Rebuild required for C++ matcher.

### DEC-013 — 2026-07-13 — Duels: legal-action pool only; no in-code strategy pick
**Status:** superseded by DEC-018  
**Context:** Heuristic strategies both *enumerate* available actions and *rank* them. Ranking contaminates exploration and blocks learning a true policy.  
**Decision:** In duels (bracket / duel combat), the Engine uses strategies **only as an availability filter** (useful + possible combat actions in the queue). **Action choice ignores heuristic relevance.**
- **Collect / explore:** uniform random among that legal set (`MlDuelBracket.ActionPolicy = random`).
- **Deploy ranking net (D9):** one forward → scores over the same legal set → argmax / softmax (`ActionPolicy = ranker`); **no** heuristic/alpha blend in duels.
**Why:** Most fundamental exploration path; the net (or random) decides; strategy code is not a teacher.  
**Consequences:** Arena/BG can keep hybrid heuristics until specialists catch up. Duel logs mark `explored=1` under random policy. Difficulty dial later can re-enable partial heuristic masks without changing this default.  
**Amendment:** DEC-014 replaces queue-only enumeration with the full spellbook pool for duels.

### DEC-014 — 2026-07-13 — Duel action pool = full spellbook (emergent learning)
**Status:** superseded by DEC-018  
**Context:** Strategy queue only exposes scripted abilities; emergent learning needs every currently castable combat spell.  
**Decision:** Default `MlDuelBracket.SpellPool = spellbook`. Candidates = known active non-passive spells that pass `CanCastSpell` on duel opponent or self (buffs), minus profession/travel/noise. Execute via `CastSpell(id, target)` — no strategy name required. Options: `queue` (old), `union`.  
**Why:** Real exploration / emergence; strategy code is not the action vocabulary.  
**Consequences:** Larger action space; ranking vocab grows; logs use lowercase spell names; rebuild required (`MlDuelSpellPool`).

### DEC-015 — 2026-07-13 — Markov + rich features first; RNN later (reusable)
**Status:** accepted  
**Context:** World-class PvP often uses temporal credit (DR chains, GCD sequences); question whether to train RNN now.  
**Decision:** Stay **memoryless (Markov)** for v1 with a **rich orthogonal feature pack** (CDs, DR, range, resources — D8). Defer RNN/GRU until the ranking head + spellbook pool plateau.  
**Reuse:** RNN **does reuse** the memoryless stack — same features-per-tick, same action vocab/mask, same rewards, same duel logger; add ordered episode sequences + hidden state at inference. Do **not** throw away current training.  
**Why:** Explicit CD/DR features capture most of what short memory would learn; RNN adds cost and needs clean long episodes first.  
**Consequences:** Direction D8 before any RNN track; when RNN starts, trainer becomes sequence-aware on top of existing CSV/`match_id` episodes.

### DEC-016 — 2026-07-13 — Duel objective: win at any cost
**Status:** accepted  
**Context:** Short survival shaping teaches turtling; user wants win as the dominant goal.  
**Decision:** For duels, `reward = short + λ_duel * terminal` with **`MlDuelBracket.TerminalLambda` ≫ short** (default **25**; |short| clamped ≤ 2). Short reward only credits **opponent pressure / kill / interrupt** — **no self-HP preservation**. Death is scored by terminal loss, not a panic short penalty. Arena/BG keep existing `MlTerminalLambda`.  
**Why:** Emergent aggression; net learns “whatever wins,” not “stay safe.”  
**Consequences:** Winning steps get ~+25 each; losing ~−25; shaping stays a weak tip.

### DEC-017 — 2026-07-14 — Ship Class + CD + DR + Range packs (self and foe)
**Status:** accepted  
**Context:** Core 12-D was only enough for a coarse baseline; c-level 1v1 needs identity, cooldowns, diminishing returns, and positioning — including **opponent** state (DEC-015 / D8).  
**Decision:** Expand `CombatFeatureVector` to **70** state floats (Core + Class + DuelCD + DuelDR + DuelRange + FoeVitals). Log to **`ml_decisions_duel_v2.csv`**. Foe CD/DR/spec/power use server-side omniscience for training. Keep legacy **20-D** PBML inference path (`ML_INPUT_DIM_V1`) until new models are trained.  
**Why:** Markov ranker cannot invent kick/DR/gap knowledge from HP alone; opponent CD/position are first-class for Arms vs Frost.  
**Consequences:** Retrain required for 78-D PBML; old 20-D models still score via core+flags only; difficulty dial may later mask packs (D7).

### DEC-018 — 2026-07-16 — Duel RL showcase curriculum: staged S0→S1→S2 (supersedes DEC-013, DEC-014)
**Status:** accepted  
**Context:** [Wayfinder map #4](https://github.com/gamesh411/mod-playerbots/issues/4) locks a reproducible duel showcase path. DEC-013 made uniform random over a legal pool the default duel policy and forbade heuristic relevance for action choice. DEC-014 made the full spellbook that pool’s default vocabulary. The curriculum needs stock Softmax over the scripted queue as **S0**, a learned ranker on that same vocab as **S1**, and a learned ranker on the spellbook as required **S2**.  
**Decision:** Duel ML policy is a **three-stage curriculum**, each stage frozen by artifact + conf profile (see map #8):
| Stage | Action vocabulary | Action choice | Conf sketch |
|-------|-------------------|---------------|-------------|
| **S0** | Scripted combat queue (`SpellPool=queue`) | **Softmax over stock strategy relevance logits** (temperature τ; deterministic argmax allowed for replay) | `ActionPolicy=softmax-stock` |
| **S1** | Same scripted queue | Learned ranking head (DEC-011); argmax or softmax over model logits | `SpellPool=queue`, `ActionPolicy=ranker`, S1 PBML |
| **S2** | Full legal spellbook (`SpellPool=spellbook`, DEC-014 mechanics) | Learned ranking head; argmax or softmax over model logits | `SpellPool=spellbook`, `ActionPolicy=ranker`, S2 PBML |

- **S0 is the showcase stock baseline** — strategies both enumerate *and* rank via existing Engine relevance; this is intentional reproducibility, not an ML teacher for later stages.
- **S1/S2 ignore heuristic relevance** for action choice (ranker only), as DEC-013 intended for learned policies.
- **DEC-013 `ActionPolicy=random` + uniform legal-pool explore** and **DEC-014 spellbook-as-default** remain in code/conf as an **archived off-curriculum path** for prior art / ablations; they are not the S0 showcase default.
**Why:** Staged artifacts must be replayable; random spellbook exploration is poor showcase material; spellbook ranker stays required but belongs at S2 after scripted-vocab learning.  
**Consequences:** Engine gains `softmax-stock` duel policy; stage profiles default `SpellPool=queue` until S2; DIRECTIONS D13/D14 and docs hub (#6) restate stages; tickets #9–#11 implement per stage. DEC-005 (arena ε-greedy), DEC-011 (ranking net), DEC-012 (bracket), DEC-015–017 unchanged. Freeze layout: DEC-019.

### DEC-019 — 2026-07-16 — Stage freeze contract: hybrid artifacts + stage cards
**Status:** accepted  
**Context:** [Stage freeze contract: artifact layout + stage card template](https://github.com/gamesh411/mod-playerbots/issues/8) on map [#4](https://github.com/gamesh411/mod-playerbots/issues/4). Every curriculum stage must be mechanically replayable; S0 has no PBML weights while S1/S2 do.  
**Decision:** **Hybrid freeze** — light half in-repo, heavy `.pbml` via GitHub Release assets keyed by sha256.

| Piece | Location / rule |
|-------|-----------------|
| Manifest | `artifacts/duel/s{N}/manifest.json` (required at freeze) |
| Conf snippet | optional `artifacts/duel/s{N}/playerbots.conf.snippet` |
| Stage card | `docs/ml/curriculum/s{N}-*.md` (human view; **manifest wins** on conflict) |
| Git tag | `stage/s{N}-<slug>` (immutable; re-freeze = new tag, never move `stage/s{N}`) |
| Conf profile | `duel-s{N}` (stable across re-freezes of the same stage) |
| data_tag | `<csv_basename>+<feature_dim>d` (e.g. `ml_decisions_duel_v2+70d`) |
| S0 policy | `policy.kind = "softmax-stock"` — no Release asset |
| S1/S2 policy | Release named as the git tag; asset `duel-s{N}-<slug>.pbml`; manifest stores `release_asset`, `sha256`, `local_name` |

**manifest.json required fields:** `stage`, `git_tag`, `git_sha`, `policy`, `conf_profile`, `data_tag`, `stage_card`, `frozen_at`; optional `conf_snippet`, `notes`.

**Stage card table fields:** Stage, Policy, Vocab, Movement, Policy artifact, Conf profile, Data tag, Git tag, Manifest path, Status (`not frozen` / `frozen`), plus Goal section.

**Why:** Replay is one tag → one manifest; docs stay readable; binaries do not bloat the upstreamable history; S0 freezes without weights.  
**Consequences:** Populate `artifacts/duel/s{N}/` and fill stage-card TBDs only when a stage freezes (S0 via [#9](https://github.com/gamesh411/mod-playerbots/issues/9)+). [#13](https://github.com/gamesh411/mod-playerbots/issues/13) maps `duel-s{N}` into `wotlk-playerbots-server` / conf.dist. VODs/metrics remain out of this contract.

### DEC-020 — 2026-07-16 — Duel farm infrastructure import inventory
**Status:** superseded by DEC-021  
**Context:** [Duel farm infrastructure import (bracket, features, logger)](https://github.com/gamesh411/mod-playerbots/issues/12). `exp/duel-rl-curriculum` is clean `origin/master` (no `src/Ai/Ml/`); duel WIP lives on `wip/duel-farm-uncommitted` atop the mixed archive tip.  
**Decision:** Path-selective import from **`wip/duel-farm-uncommitted` tip** onto `exp` — bracket, 70-D features, duel logger, spell pool, MlScorer/MlMlpModel, Engine duel hooks + Queue `Baskets`/`PopBasket`, conf/wiring, `tools/ml` trainers. Import Mode B/C hybrid strategies **dormant** (`HybridRelevanceEnabled=0`). **Do not** import arena team fill / BG strategy archive hunks. On import, rewrite duel conf defaults to DEC-018 (`SpellPool=queue`, `ActionPolicy=softmax-stock` or interim `heuristic` until Softmax lands). Keep existing `docs/ml/` hub; do not overwrite from wip docs. Full inventory: [`docs/ml/research/duel-farm-infrastructure-import.md`](research/duel-farm-infrastructure-import.md).  
**Why:** Need a coherent farm substrate for S0–S2 without pulling arena-first history or wrong showcase defaults (random+spellbook).  
**Consequences:** Execution is a follow-on AFK task blocking [#9](https://github.com/gamesh411/mod-playerbots/issues/9); Engine Softmax behaviour remains [#9](https://github.com/gamesh411/mod-playerbots/issues/9), not part of the file copy.

### DEC-021 — 2026-07-16 — Curriculum-only import (no dormant Mode B/C)
**Status:** accepted  
**Context:** Upstreamability of `exp/duel-rl-curriculum`; DEC-020 allowed Mode B/C hybrid strategies to land disabled. That still widens the PR surface with non-curriculum code.  
**Decision:** Supersede DEC-020’s “import dormant Mode B/C” clause. Import **only** code strictly necessary for the duel S0→S2 curriculum. **Do not** land `HybridRelevanceStrategy`, `PvpPolicyStrategy`, hybrid/pvp `StrategyContext`/`AiFactory` wiring, `HybridRelevanceEnabled` / dual alpha/model conf, arena ε-greedy Engine path, or BG match-end logger hooks. Slim HeuristicScores / MlScorer / logger to duel flags + single duel ranker path. Arena fill and BG strategy tweaks remain excluded. Research asset updated in place: [`docs/ml/research/duel-farm-infrastructure-import.md`](research/duel-farm-infrastructure-import.md). Execution: [#14](https://github.com/gamesh411/mod-playerbots/issues/14).  
**Why:** Cleaner upstream contribution; unused strategies are prior art on archive/WIP branches, not ballast on the curriculum line.  
**Consequences:** [#14](https://github.com/gamesh411/mod-playerbots/issues/14) must slim/rewrite on import rather than copy wip tip; Mode B/C stays recoverable from `archive/wip/pre-curriculum-2026-07-16` / `wip/duel-farm-uncommitted` if ever revived outside this map.

### DEC-022 — 2026-07-16 — S0 Softmax-stock Engine design
**Status:** accepted  
**Context:** [S0 Engine: Softmax over stock scripted combat queue](https://github.com/gamesh411/mod-playerbots/issues/9). Conf defaults already `ActionPolicy=softmax-stock` + `SpellPool=queue`; Engine still only implements `random` / `ranker` over the duel queue shell. Movement stays scripted; later stages leave stock ranking behind.  
**Decision:** Implement `ActionPolicy=softmax-stock` by extending the **existing duel queue candidate shell** in `Engine::DoNextAction` (same enumerate path as `random` / `ranker`), not a separate pipeline.

| Piece | Rule |
|-------|------|
| Softmax support (mask) | Loggable combat ∧ `isUseful` ∧ `isPossible` ∧ post-multiplier relevance > 0 |
| Logits | Raw **basket relevance** (stock Peek scores); multipliers gate membership only — they do not reweight Softmax |
| Empty support | Fall through to stock `Peek` (scripted movement / meta) |
| Temperature | Conf `AiPlayerbot.MlDuelBracket.SoftmaxTemperature`; **farm default τ = 10**; **demo / freeze τ = 0** (argmax over masked logits); τ ≤ 0 ⇒ argmax |
| Anneal | None in-engine for S0 — switch τ via conf / stage profile |
| S0 mask philosophy | Keep full stock useful/possible filters; thinning to mechanical-only legality is an S1/S2+ concern |

**Why:** Mild Softmax (τ = 10) on stock ranking bootstraps S1 data near-optimal with light coverage; τ = 0 replays stock for showcase freeze; combat-only mask preserves scripted movement; basket logits avoid baking multiplier scales into explore probs the curriculum will drop.  
**Consequences:** Execution ticket implements Softmax sample + conf key on `exp/duel-rl-curriculum`. S0 farm vs demo differ by τ only. Duel-throughput maximization for faster CSV collection is a separate ticket (conf and/or Engine), not part of this DEC.

### DEC-023 — 2026-07-16 — Duel-only farm throughput + full-resource start gate
**Status:** accepted (match/restore power list superseded by **DEC-024**)  
**Context:** [Maximize duel farm throughput for training](https://github.com/gamesh411/mod-playerbots/issues/16). Need max Arms↔Frost matches/hour for S0–S2 CSV without breaking DEC-022 Softmax-stock or scripted movement; user requires a server mode with **no other bot activity**, full HP + non-rage resources before start, and major-CD start state observed but not used as a match gate.  
**Decision:**

| Piece | Rule |
|-------|------|
| Server profile | New orchestrator profile **`duel-farm`** (`wotlk-playerbots-server`): **no** BG/arena auto-join, **no** RPG/grind, **no** capital banker tele; idle random bots only run `MlDuelBracket` at parks. Orthogonal to `-ProfileGB`. |
| Population | Level-80 fixed; class mask from `Pairs` (default Arms↔Frost); `MinRandomBots`=`MaxRandomBots` at hardware max (same scale as arena-tournament). Curriculum conf: `ActionPolicy=softmax-stock`, `SpellPool=queue`, farm `SoftmaxTemperature=10`. |
| Match hard gate | Both bots: **100% health** and **full non-rage power** (Mana / Energy / Runic Power / Focus when that is their power type). **Rage is not gated** (Warriors start/spend rage in combat). Alive, not in combat/duel/BG/arena. |
| Major CDs | **Not** a start gate — bots with full vs depleted major CDs may pair. **Observe** start state via existing DuelCD feature pack (DEC-017) plus a duel-start snapshot on the logger (per-bot major-CD readiness summary at `OnDuelStart` / first logged decision). |
| Throughput under gate | When bracket-idle at park: **instant restore** HP + non-rage powers (and clear eat/drink), then rematch. `RematchCooldownMs` is only a short post-duel debounce (farm profile **500** ms; not the resource wait). Do **not** wait on natural regen/drink. |
| Accept path | `AcceptDuelAction` (and bracket `IsIdleEligible`) use the same full-resource hard gate when `MlDuelBracket` is enabled so request/accept cannot race below full. |
| Softmax / movement | Unchanged (DEC-022). No ML movement. |

**Why:** Isolating duel-farm removes arena/BG concurrency theft; instant park restore + short debounce maximizes matches/hour while keeping every episode’s start resources comparable; logging major-CD readiness without gating preserves natural CD-desync coverage for the ranker.  
**Consequences:** Execute follow-on lands Engine restore/gate + logger snapshot on `exp/duel-rl-curriculum`, and `duel-farm` in `wotlk-playerbots-server` (`Get-ServerProfiles` / `Apply-MlDuelBracket` / ensure_running). Stage-replay profiles (#13) stay separate from this farm mode.

### DEC-024 — 2026-07-16 — Duel-start resource gate: regenerative vs build-up
**Status:** accepted  
**Context:** Correction to [Maximize duel farm throughput for training](https://github.com/gamesh411/mod-playerbots/issues/16) / DEC-023 after Death Knight review. AzerothCore treats **Rage** and **Runic Power** as build-up powers (login/out-of-combat toward 0); DK **runes** are a separate ready/cooldown pool (`Player::GetRuneCooldown`).  
**Decision:** Supersede DEC-023’s match hard gate / park-restore **power list** with this split:

| Kind | Examples | Duel-start gate | Park restore |
|------|----------|-----------------|--------------|
| Regenerative / ready pool | Mana, Energy, Focus; **all DK runes off cooldown** | **Required full / ready** | Top up mana/energy/focus; clear rune cooldowns |
| Build-up combat power | **Rage**, **Runic Power** | **Not gated** (may be 0) | Do **not** require or force full |

Health remains **100%** for both participants (DEC-023). Major ability CDs stay observe-not-gate (DEC-023).  

**Why:** Same shape as Warrior Rage: spend-to-zero combat meters are not “full to start”; DK runes are the ready pool analogous to a Rogue’s Energy bar being full.  
**Consequences:** [Execute duel-farm profile + full-resource rematch (DEC-023)](https://github.com/gamesh411/mod-playerbots/issues/17) implements this split (not “full Runic Power”).

