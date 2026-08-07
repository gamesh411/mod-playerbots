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

### DEC-025 — 2026-07-17 — S1 scripted-vocab ranker: train / deploy / freeze
**Status:** accepted  
**Context:** [S1: ranking head over scripted action vocabulary](https://github.com/gamesh411/mod-playerbots/issues/10). S0 Softmax-stock (DEC-022) and duel-farm (DEC-023/024) land data; S1 must improve on stock within the same scripted **queue** vocab (DEC-018), stay replayable (DEC-019), and leave stock as a scaffold—not a permanent teacher. Fine-grained ability-level / multi-logit ranking head over spellbook remains **S2** (DEC-011 target architecture deferred past S1 freeze).

**Decision:**

| Piece | Rule |
|-------|------|
| Vocab / policy | `SpellPool=queue`, `ActionPolicy=ranker`; ignore heuristic relevance for choice (DEC-018). Movement stays scripted. |
| Scorer (S1 freeze) | **Per-candidate scalar** (`ScoreDuel` / existing PBML path). Softmax or argmax over those scores. True multi-logit ranking head + ability-level vocab → **S2**. |
| Farm exploration | Softmax over model scores; **τ=10** farm / **τ≤0** demo-freeze; shared conf `MlDuelBracket.SoftmaxTemperature` (same dial as S0). No ε-greedy for S1. |
| Models | **Per-class** PBML (e.g. warrior / mage). Opponent class is features, not a matchup file. Same packaging when the class pool grows later. |
| Train recipe | (1) **Bootstrap:** reward / delayed outcome on S0 Softmax-stock CSVs. (2) **DAgger ×2:** ranker on-policy rollouts; label each state with **Softmax-stock τ=0** expert pick among legal queue candidates; train to imitate (CE or score-up expert). (3) **Expert-off:** further on-policy ranker farm; train on reward/win only — **no** stock expert. Full **aggregate** retrain each round (never drop prior rows). |
| Freeze gate | Both seats must improve: Arms-ranker↔Frost-stock and Arms-stock↔Frost-ranker winrates each clear **stock↔stock baseline + δ** (same bracket/conf; Softmax τ=0 stock). δ and duel count are freeze-time ops, recorded on the stage card — not a fixed constant here. Absolute 50% is **not** the gate (class imbalance). |
| Stage artifacts | DEC-019 layout: `artifacts/duel/s1/…`, conf profile `duel-s1`, git tag `stage/s1-<slug>`, stage card; manifest lists **per-class** PBML release assets + sha256. Demo conf: `ActionPolicy=ranker`, `SoftmaxTemperature=0` (or ≤0), `SpellPool=queue`. |

**Why:** Reward bootstrap alone is off-policy under stock; pure imitation never beats stock; DAgger with τ=0 stock on learner states keeps the scripted basin, then expert-off supplies winrate pressure to leave the teacher. Shared Softmax τ matches S0 ops. Per-class models scale to a wider duel pool without matchup explosion. Scalar S1 keeps the shippable scorer; spellbook-level granularity belongs at S2.

**Consequences:** Execute follow-on implements ranker Softmax(τ), expert-action logging for DAgger, trainer imitate + aggregate loop, per-class deploy paths, and eval harness vs stock↔stock baseline. Orchestration stage replay remains [#13](https://github.com/gamesh411/mod-playerbots/issues/13). S2 design locked in **DEC-026** / [#11](https://github.com/gamesh411/mod-playerbots/issues/11).

### DEC-026 — 2026-07-17 — S2 spellbook multi-logit ranker: train / deploy / freeze
**Status:** accepted  
**Context:** [S2: ranking head over full legal spellbook (atomic actions)](https://github.com/gamesh411/mod-playerbots/issues/11). S1 (DEC-025) freezes a scalar queue ranker; DEC-011’s true multi-logit head and DEC-018’s spellbook vocab land at S2. WotLK keeps distinct spell ids per rank (`spell_ranks`); utility/cast-time downranking remains real (e.g. low-rank Frostbolt snare) even though mana-efficiency downranking is obsolete. Winrate is non-transitive — beating S1 does not imply beating stock.

**Decision:**

| Piece | Rule |
|-------|------|
| Vocab / policy | `SpellPool=spellbook`, `ActionPolicy=ranker`; ignore heuristic relevance for choice (DEC-018). Movement stays scripted. |
| Scorer | **Multi-logit ranking head** (DEC-011): one forward → scores over a fixed per-class **spell-id** vocab; Softmax/argmax after a per-tick **legality mask** (`CanCastSpell`). |
| Ranks | **Every known rank is its own action** (distinct spell id). S2 does **not** collapse to highest rank per name (today’s `MlDuelSpellPool` highest-rank map is wrong for S2 and must change on execute). |
| Vocab membership | Frozen **level-80 class combat template** (non-noise spells a duel-farm 80 of that class can know), stored in the PBML. Organic leveling: max rank known ⇒ lower ranks known — no GM edge cases. Mask handles “not castable this tick.” Low-level bots may **load** the same PBML + mask; quality is not a freeze claim. |
| Out of head | Mount / move primitives (stay in movement system; ML movement out of map scope). Full item/consumable bag (optional tiny allowlist later — not S2 freeze). |
| Farm exploration | Softmax **τ=10** farm / **τ≤0** demo; shared `MlDuelBracket.SoftmaxTemperature`; **no ε** (same ops as S0/S1). |
| Models | **Per-class** PBML + frozen spell-id vocab in manifest (DEC-019). |
| Train recipe | (1) **Bootstrap** on available reward/outcome rows. (2) **DAgger ×2:** on-policy S2 rollouts; imitate **S1 ranker τ=0** on **queue ∩ legal spellbook** — expert label is the **concrete spell id** exposed by the queue Action. (3) **Expert-off:** reward/win only on full spellbook Softmax explore. Full **aggregate** retrain each round. **No** weight warm-start from S1 (labels only). |
| Freeze gate | **Stacked**, both seats: clear **S1↔S1 + δ** *and* **stock↔stock + δ** (mixed seats as DEC-025). δ and duel counts are freeze-time ops on the stage card. Absolute 50% is not the gate. |
| Stage artifacts | DEC-019: `artifacts/duel/s2/…`, conf `duel-s2`, git tag `stage/s2-<slug>`, stage card; demo `ActionPolicy=ranker`, `SpellPool=spellbook`, `SoftmaxTemperature≤0`. |

**Why:** Spell-id multi-logit is the DEC-011 architecture and the only honest key for per-rank downranking. S1 is a better DAgger teacher than Softmax-stock; expert-off is what pressures off-queue casts. Shared Softmax τ keeps farm ops one dial. Stacked freeze makes the “better than S1 and stock” claim measurable (non-transitive WRs). Class@80 template keeps DEC-019 replay stable without per-level models.

**Consequences:** Execute follow-on: multi-logit PBML schema + Engine path, stop highest-rank collapse for S2, queue Actions expose spell id for DAgger logs, trainer CE/aggregate loop, class template vocab builder, stacked eval harness. Blocked on S1 execute ([#18](https://github.com/gamesh411/mod-playerbots/issues/18)). Orchestration `duel-s2` remains [#13](https://github.com/gamesh411/mod-playerbots/issues/13).

### DEC-027 — 2026-07-17 — S1 round-2 mixed-seat result + S2 readiness caveats
**Status:** accepted  
**Context:** [#18](https://github.com/gamesh411/mod-playerbots/issues/18) DAgger round-2 after action-id (82-D) + expert-elevation fixes. Product judgment: Arms vs Frost is dominated by precise mage ability sequencing (Frostbolt → Ice Lance, dual root via player Frost Nova + Water Elemental Nova / Cold Snap resets); kiting/movement ML is important but **deferred**. Concern that Water Elemental Frost Nova needs a **ground-target** (cast + click) which S1 queue ranking does not model and S2 spellbook execute may not yet support.

**Decision:**

| Piece | Rule |
|-------|------|
| Round-2 measured gate (δ=0.02, ~2.4k matches/seat) | **Frost-ranker PASS** — mage **31.0%** vs stock 25.8% (**+5.2pp**). **Arms-ranker FAIL** — warrior **70.9%** vs stock 74.6% (**−3.7pp**). DEC-025 freeze **not** met (both seats required). |
| Remaining S1 work | Prefer **one more Arms-focused** DAgger / redeploy before abandoning S1 freeze. Mage seat is no longer the bottleneck. |
| Pivot to S2 | If Arms cannot clear stock+δ without expanding beyond scripted **queue** vocab, open/execute S2 ([#11](https://github.com/gamesh411/mod-playerbots/issues/11) design / follow-on execute) rather than endless S1 expert-off on scalar flags. |
| Movement | **Do not** start mage kite / ML movement for this matchup yet (scripted movement stays). |
| S2 open risk — pet ground Nova | Before claiming Water Elemental Frost Nova combos in S2, verify Engine can issue **ground-targeted** pet/ability casts (not only unit-targeted `CastSpell`). Track as an S2 execute prerequisite / bug if missing. |
| Artifacts | Keep 82-D PBML + logger/trainer fixes; stage card records round-2 numbers; freeze tag still TBD until both seats pass. |

**Why:** Measured evidence: scripted-vocab + action-id is enough for Frost seat uplift once imitation bugs are fixed; Arms seat still underperforms Softmax-stock. Domain skill ceiling for Frost likely needs spell-id granularity (S2) and possibly pet ground targeting — not more S1 scalar category learning. Deferring kite keeps scope honest.

**Consequences:** [#18](https://github.com/gamesh411/mod-playerbots/issues/18) stays open until Arms clears or an explicit S1-freeze waiver. S2 execute must list ground-target / Water Elemental Nova as a readiness check. Update stage card `s1-scripted-vocab-ranker.md` status to reflect asymmetric PASS/FAIL.


### DEC-028 - 2026-07-28 - S1 soft-fail waiver; pivot to S2 execute
**Status:** accepted  
**Context:** [#18](https://github.com/gamesh411/mod-playerbots/issues/18) final Arms-focused DAgger after ~2x `duel_v4` growth (~253 MB, ~1.08M rows). User call: one more retrain+eval, then pivot if gate still fails (DEC-027).

**Decision:**

| Piece | Rule |
|-------|------|
| Round-3 train | Aggregate v3+v4; per-class `--imitate-expert` then expert-off; `--max-rows 400000`, 30 epochs; `input_dim=82`. |
| Round-3 mixed gate (d=0.02, ~2.4k matches/seat) | **Arms-ranker FAIL** - warrior **68.9%** vs stock 74.6% (**-5.7pp**). **Frost-ranker FAIL** - mage **14.2%** vs stock 25.8% (**-11.6pp**). Both seats worse than round-2. |
| S1 freeze | **Soft-fail waiver** - do **not** cut DEC-019 freeze tag / `duel-s1` packaging. DEC-025 both-seat gate remains unmet. |
| Canonical S1 artifacts | Keep **round-2** PBMLs as `artifacts/duel/s1/warrior.pbml` / `mage.pbml` (frost PASS history). Archive round-3 as `*.round3-fail-20260728.pbml`. |
| Pivot | Close [#18](https://github.com/gamesh411/mod-playerbots/issues/18); proceed to S2 execute ([#19](https://github.com/gamesh411/mod-playerbots/issues/19) / DEC-026). No further S1 expert-off thrash on queue vocab. |
| Caveats carried | DEC-027 pet ground-target Nova readiness still required before S2 claims that combo. |

**Why:** Extra on-policy data + another DAgger/expert-off round did not clear Arms and regressed Frost. Matches DEC-027 judgment that scripted-queue scalar ranking has hit its ceiling for this matchup; spell-id S2 is the next lever.

**Consequences:** Stage card / artifacts README record soft-fail; map Decisions-so-far gets this pointer; S2 unblocked.

### DEC-029 - 2026-08-01 - S2 retrain scheme: win-anchored self-imitation + balanced CE
**Status:** accepted  
**Context:** [#19](https://github.com/gamesh411/mod-playerbots/issues/19) S2 freeze-fail diagnosis (`356c191b` / `0cb22754`): expert-off aggregate CE behavior-cloned tau=10 exploration junk with no reward signal and collapsed onto the marginal majority action (arms ~pure Cleave r8 / offline argmax 66.6% Auto Attack; frost 87.2% Auto Attack; WR 0% vs honest stock). A pure DAgger round caps at the S1 teacher, which itself soft-failed on Arms (DEC-028), so it cannot clear the stacked "beat S1+delta" gate.

**Decision:**

| Piece | Rule |
|-------|------|
| Label scheme | `--label-scheme win-else-expert`: CE target = own logged action on **won** episodes (win = last `terminal>0` per match/bot, eval convention), else S1 `expert_action`, else drop the row. Reward pressure beyond the teacher + rotation sanity from it. |
| Class balance | `--balance-labels sqrt`: CE row weight ~ 1/sqrt(label frequency). Counters marginal-mode argmax collapse onto always-legal actions (Auto Attack 6603). |
| Capacity | hidden 128, 60 epochs over full s2+r2+r3+eo aggregate (~1.7M rows; 796k kept warrior / 664k mage), frozen vocab 49W/220M. No weight warm-start (DEC-026). |
| Offline degeneracy gate | Before any deploy: argmax distribution over ~20k sampled real states must show a spread kit, not a single dominant junk action. Checker also reports expert-agreement. |
| Measured (offline) | Warrior: 29 distinct argmax, Auto 33.7%, Cleave 13.5%, HS 12.5%, MS/Sweeping present. Mage: 38 distinct, Auto 30.8%, Frost Nova 16.4%, Counterspell 12.2%, Frostbolt 9.1%, Ice Block 7.1%. Unbalanced variant stayed collapsed (80%/67% Auto) - balance is load-bearing. |
| Order of operations | Short mixed smoke per seat (honest Softmax-stock opponent, fresh CSV) before any full gate run; no `stage/s2` cut on smoke numbers. |

**Why:** Labels in the data are healthy (expert warrior Auto only 14.8%, spread kit) but a 70-D MLP underfits the state-conditional and plain CE degenerates to predicting the marginal everywhere; at tau=0 the always-legal majority action wins argmax in every state. Win-filtering alone keeps the same marginal (winners of junk-vs-junk still spam junk). Frequency balancing changes which action wins argmax per state, which is exactly the tau=0 deploy behavior.

**Consequences:** `train_spellbook_ranker.py` gains `--label-scheme` / `--balance-labels`; retrained heads replace `artifacts/duel/s2/{warrior,mage}.pbml` (prior expert-off heads kept as `*.pre-dec029-20260801-*.pbml`). Smoke + gate numbers land on #19. If seats still fail badly, next lever is another on-policy DAgger farm round with the balanced trainer, not more offline thrash on the same data.

### DEC-030 - 2026-08-01 - Auto-attack toggles are engine scaffolding, not S2 head actions
**Status:** accepted  
**Context:** DEC-029 arms smoke (fresh CSV, honest Softmax-stock mage, tau=0, 1150 matches): warrior WR **1.4%**, stacked FAIL. Live action mix: Cleave r8 **69.6%**, Auto Attack 1.3% - divergent from the offline argmax (Auto 33.7%). Mechanical cause, not just policy quality: the spellbook ranker block short-circuits the tick, so nothing else ever starts melee swings, and on-next-melee picks (Cleave / Heroic Strike) never resolve. User input: Attack / Auto Shot / wand Shoot are **toggle** abilities (persistent auto-repeat state), not instant/casted/channelled spells - they do not belong in a pool of castable actions.

**Decision:**

| Piece | Rule |
|------|------|
| Melee toggle | Engine maintains melee auto-attack on the duel opponent inside the spellbook ranker block (`self->Attack(opponent, true)` when victim differs), as engagement scaffolding alongside scripted movement (extends DEC-026 "out of head"). |
| Candidate pool | `MlDuelSpellPool` excludes 6603 (melee Attack) and every `SPELL_ATTR2_AUTO_REPEAT` spell (Auto Shot 75, wand Shoot 5019, ranged Shoot 3018, Throw 2764). |
| Labels | Trainer `--drop-labels 6603 3018 5019 75 2764`: rows resolving to toggle labels are dropped (win-self falls back to expert label where present). |
| Vocab | Frozen vocab files keep the ids (PBML compatibility); the logits are dead weight at runtime. |
| Wand scaffolding | Auto-maintaining wand/ranged toggles for casters is deferred - not needed for the Arms/Frost gate. |

**Why:** A toggle flips persistent state; ticking it repeatedly is a no-op decision that starves real casts, and its always-legal status makes it the natural argmax sink for an underfit head. Removing toggles from both the action space and the labels makes every head decision a real cast, and scaffolded melee makes on-next-melee specials actually resolve.

**Consequences:** Worldserver rebuild required; heads retrained with toggle labels dropped; smoke rerun per DEC-029 order of operations. Prior smoke CSV archived as `ml_decisions_duel_mixed_arms_ranker.dec029-smoke-20260801.csv`.

### DEC-031 - 2026-08-01 - On-next-swing spells are masked while one is queued
**Status:** accepted  
**Context:** [#19](https://github.com/gamesh411/mod-playerbots/issues/19) dec030 smoke: Arms lands melee now but stays in Cleave-r8 spam (69.6% of rows).
User input: on-next-swing spells (Heroic Strike / Cleave / Maul) are similar to toggle skills and should be handled with the same care - they queue on the swing timer instead of resolving on pick, so re-picking one is a no-op decision.

**Decision:**

| Piece | Rule |
|------|------|
| Candidate pool | `MlDuelSpellPool` masks every `SPELL_ATTR0_ON_NEXT_SWING` / `SPELL_ATTR0_ON_NEXT_SWING_NO_DAMAGE` spell while `CURRENT_MELEE_SPELL` is pending: pick once, then the slot stays masked until the queued swing lands or clears. |
| Head membership | Unlike DEC-030 toggles they stay head actions - queueing a swing special is a real rage/positioning decision; only the redundant re-pick is removed. |
| Labels | No `--drop-labels` change; masking fixes future data at the source. |

**Why:** While a swing special is queued, every further pick of it is dead - the collapse sink DEC-030 removed for toggles, reproduced one level up.
Masking at legality level removes the degenerate action from both live play and freshly farmed data without losing the genuine decision.

**Consequences:** Worldserver rebuild; existing CSVs still carry spam rows (win-anchored labeling + balancing cope); fresh farms are clean.

### DEC-032 - 2026-08-01 - Pet paths resolve the guardian Water Elemental (Freeze claimable)
**Status:** accepted  
**Context:** User observation on #19: the frozen-tau sparring mage never uses the Water Elemental, and its Freeze may never be used at all.
Measured: Freeze 33395 appears **zero** times across ~800k mage rows including tau>0 exploration where Summon 31687 was cast 600+ times.
Root cause: without Glyph of Eternal Water the elemental is summoned as a **Guardian with a Unit-high guid** (`SummonGuardian` -> `Map::SummonCreature` -> `GenerateLowGuid<HighGuid::Unit>`), and `Player::GetPet()` rejects non-Pet guids - so `CanCastPetSpell` / `CommandPetCastSpell` / the spellbook pet loop never saw the pet, and 33395 never entered the data or the vocab (DEC-027's command-cast path was dead code in practice).

**Decision:**

| Piece | Rule |
|------|------|
| Resolution | All playerbots pet-cast paths resolve the pet via `Unit::GetGuardianPet()` (covers both the unglyphed Guardian and the glyphed real Pet); spell knowledge via virtual `HasSpell`. |
| Candidate pool | Guardian branch iterates `Creature::m_spells` (creature_template_spell: Waterbolt 31707, Freeze 33395). |
| Autocast exclusion | Pet spells currently autocast-enabled (Waterbolt via the core summon script) are excluded from the head - the pet AI already repeats them, so commanding them is a toggle-like no-op (DEC-030 care). Freeze is not autocast and stays a command decision. |
| Vocab | 33395 enters the mage vocab at the next data-derived vocab rebuild (DEC-026 allows it: no warm start). |
| Features | No pet-state feature is added yet; summon/Freeze timing is only partially encoded via cooldown legality. Flagged as fog on the map. |

**Why:** The pet-root combo (Frost Nova / Freeze into Deep Freeze or shatter) is a core Frost duel line the S2 head could never express; the summon itself was legal but its payoff was unreachable, so no training scheme could value it.

**Consequences:** Worldserver rebuild; mage vocab grows on next retrain; summon usage at tau=0 remains a policy-quality question for win-anchored training (stock queue seats do summon via the `no pet` trigger).

**Measured refinement (same day):** the live probe showed the elemental IS a Pet object on this core (`isPet=1`) but `hasFreeze=0` - its **PetSpellMap never learns the command spells**; the client pet bar casts them from `creature_template_spell` instead. So spell knowledge, not the guid class, was the operative blocker. Fix: pet paths accept PetSpellMap **or** creature-template spells (`GuardianKnowsSpell`), and the pool collects the union. The `GetGuardianPet` resolution stays (covers the guardian shape on cores where the unglyphed elemental is not a Pet).

### DEC-033 - 2026-08-01 - Exploration-first mixed farm with win-anchored retrain
**Status:** accepted  
**Context:** User directive on #19: lean much more heavily on exploration and win-anchored training.
The Arms DAgger teacher projection is degenerate on live chase states (79% Cleave r8 expert labels), so imitation pressure cannot beat stock; dec030 smoke: Arms 1.9% / Frost 15.6%, both stacked FAIL.

**Decision:**

| Piece | Rule |
|------|------|
| Ranker-seat exploration | Mixed farms run the ranker seat at `SoftmaxTemperature` tau=2.5 (policy-shaped exploration around the current head), logging to fresh `ml_decisions_duel_s2_expl_{arms,frost}.csv`. |
| Honest stock seat | New `AiPlayerbot.MlDuelBracket.StockSoftmaxTemperature` (default -1 = follow SoftmaxTemperature) pins queue Softmax-stock seats to tau=0 during exploration. While >= 0 the Engine also refuses spellbook-explore for model-less classes, so a mixed "stock" seat can never fall into uniform spellbook junk again. |
| Warrior labels | `--label-scheme win-only` (new): own action on won episodes, everything else dropped - no fallback to the degenerate teacher. |
| Mage labels | Stays `win-else-expert` (its teacher projection is not range-starved). |
| Balance / capacity | DEC-029 unchanged: `--balance-labels sqrt`, hidden 128, no warm start; vocab rebuilt from data (picks up Freeze per DEC-032). |
| Vocab growth | Out-of-vocab legal candidates score -inf from the frozen head; while tau>0 the Engine floors them to the weakest in-vocab logit so they stay explorable (else the data-derived vocab could never grow). tau=0 keeps them masked. |
| Gate | Freeze/gate evals stay tau=0 on the existing mixed CSV names; exploration data never mixes into gate CSVs. |

**Why:** With the teacher signal broken for Arms, the only trustworthy label source is the policy's own winning behavior, and that needs coverage: exploration around the current head visits the states (gap closers, swing specials that now resolve, pet lines) that win-anchored CE can then reinforce.

**Consequences:** Orchestrator `duel-farm` mixed profile gains the explore/gate split (`config.ps1`); retrain order per class: exploratory mixed farm -> win-anchored retrain -> offline degeneracy gate -> tau=0 smoke (DEC-029 order preserved).

### DEC-034 - 2026-08-01 - Shapeshift-form spells are out of the S2 head until a form feature exists
**Status:** accepted  
**Context:** First DEC-033 warrior win-only retrain: offline argmax collapsed onto the three stances (Battle 40.7% / Berserker 32.8% / Defensive 26.5%, distinct=3).
Stances are always-legal persistent state flips - the DEC-030 toggle family - and the 70-D feature vector has **no form/stance bit**, so the head cannot condition on the state a stance pick changes; stance actions are unlearnable noise that soaks up argmax.

**Decision:**

| Piece | Rule |
|------|------|
| Candidate pool | `MlDuelSpellPool` excludes any spell applying `SPELL_AURA_MOD_SHAPESHIFT` (warrior stances, druid forms). Form control stays scripted/default, like movement (DEC-026) and melee auto-attack (DEC-030). |
| Labels | Warrior retrains add `--drop-labels 2457 2458 71`. |
| Balance | `--balance-labels inv` for the thin win-only label set (sqrt left Heroic Strike at 99.6% argmax; inv yields 33 distinct with a Charge/Pummel/Mortal Strike/Intercept-led kit). |
| Revisit | A form/stance feature (with the pet-state feature already in map fog) is the unlock for learned stance dancing; until then Intercept-from-Berserker lines stay unreachable, as they already were. |

**Why:** An action whose precondition and effect are invisible to the features cannot be state-conditionally learned; keeping it in the head only re-creates the DEC-029/030 argmax sink with a different id.

**Consequences:** Worldserver rebuild (pool exclusion); `warrior.dec033b.pbml` (win-only + inv balance) deploys as canonical `warrior.pbml`; offline degeneracy checker gains `--exclude` to mirror runtime masks.

### DEC-035 - 2026-08-05 - S2 soft-fail waiver; movement-first pivot (supersedes charting sequencing decision A)
**Status:** accepted  
**Context:** [#19](https://github.com/gamesh411/mod-playerbots/issues/19) DEC-033 explore -> win-anchored loop plateaued at round 3 (Arms 58.6% / Frost 24.7% at tau=0 vs honest stock, both stacked FAIL; [round-3 handoff](research/handoff-2026-08-02-dec033-round3-plateau.md)).
The binding constraint on the statue-movement world is feature starvation ([#25](https://github.com/gamesh411/mod-playerbots/issues/25)).
User call on the #25 grilling: pursue learned movement (M-track) instead of extending the statue-world feature vector.

**Decision:**

| Piece | Rule |
|------|------|
| S2 freeze | **Soft-fail waiver** - do **not** cut a DEC-019 `stage/s2` tag or `duel-s2` packaging. DEC-026 both-seat stacked gate remains unmet. |
| Canonical S2 artifacts | `dec033r3` heads stay canonical (`artifacts/duel/s2/{warrior,mage}.pbml`); smoke CSVs archived as `*.dec033r3-smoke-20260802.csv`. |
| No further S2 rounds | No explore/retrain rounds on the 70-D statue-world vector (round-3 handoff recommendation stands). |
| Pivot | Close [#19](https://github.com/gamesh411/mod-playerbots/issues/19); the movement track is the main line: M0 design ([#20](https://github.com/gamesh411/mod-playerbots/issues/20)) -> M0 execute ([#21](https://github.com/gamesh411/mod-playerbots/issues/21)) -> M1 ([#22](https://github.com/gamesh411/mod-playerbots/issues/22)/[#23](https://github.com/gamesh411/mod-playerbots/issues/23)) -> M2 ([#24](https://github.com/gamesh411/mod-playerbots/issues/24)). |
| Sequencing | Charting **sequencing decision A** ("S2 freezes against the unchanged movement world before M0 execute") is superseded: #21 is no longer blocked by #19, only by the M0 design DEC. |
| Feature extension | [#25](https://github.com/gamesh411/mod-playerbots/issues/25) / [#26](https://github.com/gamesh411/mod-playerbots/issues/26) parked: unclaimed, re-blocked behind M1 execute (#23); pet-state / form-stance features land at M2 charting, trained on movement-active data. |

**Why:** The win-anchored CE loop converged on the current features; Arms' remaining -16.0pp is chase behavior - movement - and the round-3 handoff already flagged movement-adjacent features as the likely Arms lever.
Extending statue-world features buys at best an epoch-1 gate pass, while M0 opens eval epoch 2 and M2 retrains the ability head anyway; feature work is better spent once movement-active data exists.

**Consequences:** S2 stage card + `artifacts/duel/s2/README.md` record the waiver; map Decisions-so-far gets this pointer; M0 design (#20) is the next frontier ticket.

### DEC-036 - 2026-08-05 - M0 movement substrate: packet-driven intent executor + scripted intent movers
**Status:** accepted  
**Context:** [#20](https://github.com/gamesh411/mod-playerbots/issues/20) M0 design, first ticket of the movement-first pivot (DEC-035).
Locks the movement substrate ahead of M0 execute ([#21](https://github.com/gamesh411/mod-playerbots/issues/21)).
Supersedes the "movement stays scripted" boundary of DEC-022/023/025/026 **for the M-track**; those entries stand as historical record.

**Decision:**

| Piece | Rule |
|------|------|
| Intent vocabulary | 9-way (hold + 8 compass points), foe-bearing-relative, parameterless, ~0.5 s horizon, re-issued each movement subtick (charting-locked). |
| Motion mechanism | **Direct velocity control via synthesized client movement packets** through the bot's own `WorldSession` (`MSG_MOVE_START_*` / `MSG_MOVE_HEARTBEAT` / `MSG_MOVE_STOP` with executor-integrated position, move flags, orientation). No MotionMaster splines on the movement channel. Relay, fall handling, and server-side state come from the normal movement handler; core patches limited to bot movement-validation bypass, `#ifdef MOD_PLAYERBOTS`. |
| Subtick | 100 ms, riding the combat feature cache TTL (`AiPlayerbot.MlDuelMovement.SubtickMs = 100`). Ability loop untouched. |
| Safety clamps | 8 foe-relative walkability probes per subtick (ground height delta + LoS at `ProbeRangeYd = 4`), computed once and shared between the feature pack and clamping. Invalid step (>2 y drop or wall) **slides** onto the nearest valid 45-degree neighbor direction; hold only if none valid. |
| Facing solver | Foe kept within **+-70 degrees** of facing while casting (20-degree margin inside the core `HasInArc(pi)` +-90). Retreat bearings inside the limit resolve to full-speed strafe-away; **jump-turn** fires only on dead-away retreat with an instant queued (flip at apex, cast, restore facing before landing), **atomic until landing**. Hardcasts and channels suppress movement (STOP, resume after). Backpedal never solver-chosen; instant turns; airborne preserves the velocity vector. |
| M0 Arms chase | Intent toward foe when out of melee; hold in melee. No orbit-for-Overpower. |
| M0 Frost kite | Foe snared/rooted and d < 30 y -> retreat (bank distance). d < 15 y -> retreat (strafe-kite). 15-30 y -> hold, stand and cast. d > 30 y or no LoS -> approach. |
| Legacy movers | Upstream `MovementActions` untouched; masked in duels while the movement channel is enabled (mirrors the ML combat mask). |
| Features / schema | **CF_MOVE pack, indices 70-89** (kinematics 8: self/foe speed frac + heading rel, facing offsets, closing speed, airborne; impairment 4: self/foe snare frac + rooted; probes 8). All angles foe-bearing-relative, speeds normalized to base run. Movement head input = 90-D (state only). CSV schema **`duel_v5`** adds the pack plus log-only `realized_heading`, `movement_intent`, `expert_movement_intent`. Ability-head `ML_INPUT_DIM` (82) unchanged. |
| Conf | `AiPlayerbot.MlDuelMovement.{Enable = 0, SubtickMs = 100, ProbeRangeYd = 4}` in conf.dist; the orchestrator `duel-farm` profile sets `Enable = 1`. |
| Throughput gate | M0 execute must hold duels/hour >= **90%** of the DEC-023/024 farm baseline; on fail, shrink probe work (8 -> 4 probes, cache on alternate subticks) and re-measure. |

**Why:** Client-authentic kinematics (front-arc speed model, strafe-kiting, jump-turns) are exactly the behaviors the M-track exists to learn; spline movement cannot express them, and the session-packet path gets relay and state handling for free instead of re-implementing it.

**Consequences:** M0 execute (#21) is unblocked and lands this design; FEATURES.md gains the CF_MOVE pack section; M0 freezes per DEC-019 as a code+conf sentinel (no PBML) and opens eval epoch 2.

### DEC-037 - 2026-08-05 - Fair rematch: zero Rage / Runic Power and clear all cooldowns at duel end
**Status:** accepted  
**Context:** user directive during M0 execute ([#21](https://github.com/gamesh411/mod-playerbots/issues/21)).
Supersedes the DEC-023/024 carryover behavior for build-up pools and the keep-cooldowns default; the DEC-024 *match-start gate* split (Rage / Runic Power ungated, regenerative pools full) stands unchanged.

**Decision:**
- `RestoreForRematch` zeroes **Rage** and **Runic Power** (build-up pools) instead of leaving them banked, so a Warrior / DK cannot open the next duel with resources the other class has no equivalent of. Regenerative pools stay topped; DK runes stay cleared.
- `AiPlayerbot.MlDuelBracket.ResetCooldownsOnDuelEnd` defaults to **1** (code default, conf.dist, and the orchestrator duel-farm profile): every duel starts from a clean cooldown slate.

**Why:** rematch fairness — carryover rage/RP and rolling cooldowns made consecutive duels state-dependent, biasing winrates and training data toward whoever ended the previous duel resource-rich.

**Consequences:** duel-farm data collected after this change is not directly comparable to pre-DEC-037 CSVs (opener distributions shift); the M0 throughput gate control and movement runs are both measured under the new rules.

### DEC-038 - 2026-08-06 - M0 throughput gate waiver at 84% + farm cycle overhaul
**Status:** accepted  
**Context:** M0 execute ([#21](https://github.com/gamesh411/mod-playerbots/issues/21)) throughput gate (DEC-036: movement-on >= 90% of movement-off, same rules/binary/protocol).
Measured over six paired 20-minute windows (fresh park reset + 10-minute stabilization each; DEC-037 rules; identical binaries per round).

**Decision:** waive the 90% gate at **84.2%** and freeze M0.
Final round: movement **1,277** matches / 20 min (3,831 duels/hour) vs control **1,516** (4,548/hour).

**Why acceptable:**
- The gate protects farm usability for M-track training; 3,831 duels/hour under DEC-037 fair-rematch rules is 2.25x the pre-optimization fair-rules farm (1,698/hour) and produces ~90k duels/day.
- Movement duels are *faster* (mean 48s vs 70s control) and the server idles (~16% of one core); the residual gap is rematch-cycle geometry — kite-range duel endings pay one teleport tick per rematch that melee-contact endings never do — not executor overhead.
- The movement arm's rate was still accelerating at window close (57.8/min at T0 -> 63.9/min window average), so 84% is a conservative estimate of steady state.

**Farm throughput fixes landed while chasing the gate** (all benefit movement-off farms too; control rose 1,698 -> 4,548/hour across rounds):
- DEC-036 prescribed probe remediation: 4-of-8 alternate-half refresh, no probing while holding.
- Executor move-flag hygiene: flags never outlive the executor (stale isMoving() starved matchmaking).
- Rematch re-anchoring: drifted pairs (60 y from pad) teleport back between duels, landing together at one pad point (kills duel-chain drift, the "run far then snap back" behavior).
- Adjacent-teleport rematch instead of walk-in; near-teleport simulated latency (1-2 s) dropped to 100 ms in bracket mode.

**Consequences:** M0 freezes per DEC-019 (code+conf sentinel); eval epoch 2 opens; the M1 farm inherits the overhauled cycle. Revisit the 90% bar only if M1 farming proves data-starved.

### DEC-039 - 2026-08-06 - M1 movement ranker: train / deploy / freeze
**Status:** accepted  
**Context:** [#22](https://github.com/gamesh411/mod-playerbots/issues/22) M1 design, second ticket of the movement-first pivot (DEC-035).
M0 (DEC-036/037/038) froze the packet executor + scripted intent movers and opened eval epoch 2.
Charting locked the shape: per-class 90-D / 9-logit heads, Softmax tau=10 farm / tau=0 demo, DAgger from the M0 scripted teacher then expert-off reward fine-tune, potential-based range-control shaping, both-seat uplift vs the M0 baseline.
This DEC locks the remaining knobs: exact potentials and weights, movement-row logging, round counts and dataset sizes, label schemes, freeze gate and eval protocol.
A code fact forces the logging piece: movement columns currently ride ability-decision rows only (~1 Hz, biased toward cast moments), while the movement channel decides every 100 ms.

**Decision:**

| Piece | Rule |
|------|------|
| Movement rows | Dedicated movement CSV `ml_movement_duel_v1.csv` - the movement head does not train on ability rows. Columns: match id, bot guid + class, 90-D features, `movement_intent`, `expert_movement_intent`, `realized_heading`, reward, terminal. Logged every 5th subtick (500 ms, the intent horizon; conf `LogEveryNSubticks = 5`) plus on every intent change; terminal backfilled per match/bot exactly like ability rows. |
| Shaping | Potential-based, gamma = 1: row reward = Phi(now) - Phi(previous logged row) + `TerminalLambda` * terminal. Deltas are taken between consecutive *logged* rows, so the telescoped sum Phi(end) - Phi(start) survives subsampling. Potentials live in [0,1] and are role-derived by seat. |
| Arms potential | Phi_A = 1 while the foe is in melee range (core `IsWithinMeleeRange`), else clamp01(1 - (d - 5)/25) - linear to 0 at 30 y. |
| Frost potential | Phi_F = LoS bit * distance trapezoid: 0 at d <= 5; ramp (d - 5)/10 up to 1 at 15; 1 across 15-30 (the DEC-036 stand band); ramp (35 - d)/5 down to 0 at the 35 y leash. |
| Terminal weight | `AiPlayerbot.MlDuelMovement.TerminalLambda = 5.0`: win/loss (+-5) dominates the +-1 telescoped shaping range (same terminal-dominant doctrine as `MlDuelBracket.TerminalLambda`, rescaled to [0,1] potentials). |
| Model / trainer | Per-class PBML, input 90, 9 logits, hidden 64 (escalate to 128 only on a degeneracy-gate failure), epochs 40, lr 1e-2, seed 0. New `tools/ml/train_movement_ranker.py` sharing the PBML writer. `--balance-labels sqrt` in every round (DEC-029: balance is load-bearing; hold/toward are the always-sensible majority intents) and `--max-rows 400000`; full aggregate retrain each round, never drop prior rows (DEC-025). |
| Round B: bootstrap | Behavior-clone the frozen M0 farm: CE with label = `expert_movement_intent` on M0 on-policy movement rows; >= 1M rows per class before training (~3 h of the DEC-038 farm at ~0.7M movement rows/hour). |
| DAgger x2 | Deploy ranker movement at tau=10; abilities pinned to the S0 Softmax-stock sentinel (tau=10) on both seats. The scripted teacher keeps running every subtick and is logged as `expert_movement_intent`. >= 1M fresh rows per class per round; retrain CE on the aggregate with expert labels. |
| Reward fine-tune (expert-off) | One round, label scheme `win-else-expert` on movement rows (own intent on won episodes, else teacher intent; win = last terminal > 0 per match/bot) + sqrt balance - the DEC-029-proven scheme with the M0 mover as fallback teacher. Escalation lever only if this plateaus: reward-weighted CE, row weight = clip(1 + 0.5 * tanh(episode shaped return), 0.5, 1.5). |
| Deploy conf | `AiPlayerbot.MlDuelMovement.Policy = scripted \| ranker` (default scripted), `AiPlayerbot.MlDuelMovement.SoftmaxTemperature = 10` (demo <= 0 argmax), `AiPlayerbot.MlModelPathDuelMovement.{Warrior,Mage}` (mirrors the DEC-025 per-class paths). Ability channel conf untouched. |
| Offline degeneracy gate | Before every deploy (DEC-029 pattern), argmax over >= 20k sampled real states. Bootstrap head: >= 90% held-out agreement with the teacher and the mover band structure reproduced per distance band (Frost: retreat < 15, hold 15-30, approach > 30 or no-LoS; Arms: toward out of melee, hold in melee). Later heads: argmax must stay state-conditional - no unconditional single-intent mode. |
| Freeze gate | Both seats, mixed-seat protocol vs a **fresh epoch-2 M0<->M0 baseline** (same binary, DEC-037 rules, abilities stock tau=0, movement tau=0): warrior WR (M1-move warrior vs M0-move mage) >= baseline warrior WR + delta, and mage WR (M0-move warrior vs M1-move mage) >= baseline mage WR + delta. delta = 0.02, >= 2.4k matches per seat (S-track ops). Absolute 50% is not the gate. |
| Movement-quality metrics | Stage card, report-only: Arms melee uptime, Frost cast-band uptime (15-30 y with LoS), mean foe distance per seat, snare-sprint and jump-turn counts, teacher-disagreement rate. |
| Anti-thrash rule | At most 2 extra seat-focused rounds beyond the recipe before a waiver/pivot DEC (S-track lesson: DEC-028/DEC-035). The 9 -> 17-way resolution knob is an escalation inside those rounds only if the movement-quality metrics show quantization binding. |
| Stage artifacts | DEC-019: `artifacts/duel/m1/manifest.json` + per-class movement PBML; git tag `stage/m1-<slug>`; stage card `docs/ml/curriculum/m1-movement-ranker.md`; farm conf `duel-farm` + `Policy = ranker`; demo `Policy = ranker`, `SoftmaxTemperature <= 0`; data tag `ml_movement_duel_v1`. Stage-replay profile `duel-m1` remains [#13](https://github.com/gamesh411/mod-playerbots/issues/13). |

**Why:**
- Ability rows undersample the 100 ms movement channel ~10:1 and bias the state distribution toward cast moments; a dedicated CSV at the 500 ms intent horizon keeps volume bounded (~0.7M rows per farm-hour) while consecutive-row deltas preserve the shaping telescope.
- The potentials mirror the DEC-036 mover thresholds exactly, so the shaping optimum and the DAgger teacher pull in the same direction instead of fighting.
- Gamma = 1 potential-based shaping telescopes to Phi(end) - Phi(start) and cannot change the optimal policy w.r.t. the terminal objective.
- Win-anchored balanced CE replaces the "reward/win only" expert-off that failed twice on the S-track (DEC-028 regression, DEC-029 collapse); the M0 mover is a healthier fallback teacher than S1 was, since it is the policy M1 must beat.
- Pinning the ability channel to the S0 sentinel on both seats for farm and gate makes movement the only variable being measured.

**Consequences:** M1 execute ([#23](https://github.com/gamesh411/mod-playerbots/issues/23)) lands: movement CSV logger + new conf keys, teacher-alongside-ranker logging in the executor, `train_movement_ranker.py`, movement PBML load path (90-D / 9-logit), degeneracy checker + movement-quality metrics in eval tooling, freeze per this DEC. #25/#26 stay parked behind #23 (DEC-035); M2 charting (#24) picks up ability-head co-adaptation once M1 freezes.

### DEC-040 - 2026-08-06 - Stage-replay demo profiles in the orchestrator (duel-s0/s1/s2/m0)

**Context:** [#13](https://github.com/gamesh411/mod-playerbots/issues/13) orchestration ticket.
DEC-019 reserved conf profiles `duel-s{N}` for stage replay; the movement pivot (DEC-035/036) reshaped the stage set to S0/S1/S2 + M0, with M0 the only executed freeze so far (DEC-038).
A demonstrator needs one command per stage that pins conf + artifact paths without hand-editing conf.

| Decision | Detail |
|----------|--------|
| Mechanism | `wotlk-playerbots-server` `-ServerProfile duel-s0 \| duel-s1 \| duel-s2 \| duel-m0`; each rides the existing duel-farm tuner with per-stage pins (`Get-DuelStageReplayProfiles`) overriding the `DuelFarm*` knobs for that configure pass (`Set-DuelStageReplayOverrides`). No forked farm plumbing. |
| Canonical pins | duel-s0: `softmax-stock`/`queue`, no PBML (S0 sentinel, DEC-022). duel-s1: `ranker`/`queue`, `artifacts/duel/s1/{warrior,mage}.pbml` (canonical round-2, DEC-028). duel-s2: `ranker`/`spellbook`, `artifacts/duel/s2/{warrior,mage}.dec033r3.pbml` immutable snapshots - never the live-learner `s2/{warrior,mage}.pbml` slots (DEC-035). duel-m0: S0 sentinel + `MlDuelMovement.Enable=1` (DEC-036). |
| Demo ergonomics (all stages) | tau=0 argmax both seats (DEC-022/025/026 demo rule); 10 bots (`DuelStageReplayBotCount`) with rndbot accounts kept farm-sized so farm <-> demo switches never churn accounts; rematch 3000 ms; `ThrottleBroadcast=0` (full 10 Hz broadcasts, DEC-038 demo note); visibility 90 (no farm relief); teacher paths cleared (no DAgger logging); per-stage CSV `ml_decisions_duel_demo_<stage>.csv` so farm CSVs stay clean. |
| Binary pinning | Stages with an executed DEC-019 freeze (today: duel-m0 -> `stage/m0-packet-executor`) warn when the module HEAD differs from the freeze tag and print the exact checkout + `-RebuildServer` commands; the orchestrator never auto-checkouts the dev tree. Unfrozen S-stages replay their canonical pins on the current binary - an eval-epoch-2 caveat, not an exact-freeze replay. |
| Extension | Each future freeze (duel-m1 per DEC-039, then M2) adds one `Get-DuelStageReplayProfiles` row + the `-ServerProfile` ValidateSet entries at freeze time. |

**Why:**
- Reusing the duel-farm tuner keeps one code path for parks, class rebalance, gear, and verification; a per-stage fork would drift.
- Session-scoped variable overrides are safe because phase 06 dot-sources `config.ps1` fresh on every apply, so demo knobs cannot leak into a later farm apply.
- Immutable snapshot pins mean a demo replays the decided stage even while the farm keeps retraining the live-learner slots.
- No auto-checkout: reproducibility guidance must not destroy uncommitted module work.

**Consequences:** #13 closes; landed in `wotlk-playerbots-server` `8168159`.
Stage cards' conf-profile rows now name their replay profiles; `duel-m1` lands with the M1 freeze ([#23](https://github.com/gamesh411/mod-playerbots/issues/23)).

### DEC-041 - 2026-08-07 - M1 freeze: mage uplift pass, warrior parity waiver

**Status:** accepted  
**Context:** [#23](https://github.com/gamesh411/mod-playerbots/issues/23) M1 execute, freeze gate per DEC-039.
Recipe ran in full: bootstrap BC (1.17M/1.05M rows), DAgger x2 (1M+ fresh rows/class/round), one win-else-expert round, every deploy behind the offline degeneracy gate.
Fresh epoch-2 M0<->M0 baseline: warrior 39.3% / mage 61.5% (2.7k matches, abilities stock tau=0, DEC-037 rules).

**Gate trail (>= 2.4k matches per run, movement tau=0):**

| Run | Warrior WR | Mage WR | Verdict |
|-----|-----------|---------|---------|
| M0<->M0 baseline | 39.3% | 61.5% | thresholds 41.3% / 63.5% (delta 0.02) |
| M1 r3-eo warrior seat | 36.9% | - | FAIL (-2.4pp vs baseline) |
| M1 r3-eo mage seat | - | **63.8%** | **PASS** (+2.3pp) |
| Anti-thrash 1: reward-weighted CE warrior | 32.1% | - | FAIL (escalation lever hurt) |
| Anti-thrash 2: r2-dagger warrior clone | 40.3% | - | parity (+1.0pp, within noise) |

**Decision:** freeze M1 with an asymmetric head set - both seats learned, one uplift and one parity:

| Piece | Rule |
|------|------|
| Canonical mage head | `artifacts/duel/m1/mage.pbml` = r3-eo (win-else-expert; +2.3pp WR, cast-band uptime 16.3% -> 19.0%, mean kite distance 9.7 -> 11.5y at gate) |
| Canonical warrior head | `artifacts/duel/m1/warrior.pbml` = r2-dagger clone (parity waiver: no learned warrior head beat the scripted chase; the near-optimal 100ms re-aim chase leaves no headroom, and win-anchored labels only added noise - r3-eo -2.4pp, reward-weighted -7.2pp) |
| Waiver rationale | Anti-thrash budget (2 rounds) exhausted per DEC-039; parity is certified by measurement, not assumed. The mage seat is where movement intelligence has room (kite geometry, snare windows) and it cleared the uplift gate properly. |
| Snapshots kept | r0-bc, r1-dagger, r2-dagger, r3-eo, r3-eo-rw per class alongside the canonical slots |

**Why:**
- DEC-028/DEC-035 precedent: record the miss, freeze the best certified artifact, never ship an uncertified head.
- A behavior clone of a near-optimal scripted policy measures at parity (+1.0pp, ~1 sigma); shipping it keeps the M1 story "both seats learned" without regression.
- The mage uplift is corroborated by movement-quality metrics, not just WR.

**Consequences:** freeze executed per DEC-019 (`stage/m1-movement-ranker`, manifest, stage card `m1-movement-ranker.md`, `duel-m1` replay profile in the orchestrator). Eval epoch 2 continues. #24 (M2 charting) unblocks; #25/#26 return to the frontier. Executor fidelity fixes landed during this execute (navmesh-validated probes/steps, dead-bot flag hygiene, rooted-unit packet guards) apply from `0508663c`/`faaf3687` onward; client-freeze investigation split out to [#27](https://github.com/gamesh411/mod-playerbots/issues/27).

### DEC-042 - 2026-08-07 - M2 design: ability-head co-adaptation on the movement-active world

**Status:** accepted  
**Context:** [#24](https://github.com/gamesh411/mod-playerbots/issues/24) M2 charting, unblocked by the M1 freeze (DEC-041).
The frozen S-track ability heads learned spell values against a statue-movement world; M2 retrains the ability head on movement-active farm data (frozen M1 movement in both seats), on the feature vector the movement era finally provides.

**Decision:**

| Piece | Rule |
|------|------|
| Head | S2-style spellbook multi-logit head (DEC-026 arch), trained fresh - no warm start from dec033r3. Vocab rebuilt from movement-active data; DEC-030/031/032 masks carry forward; DEC-034 stance exclusion revisited only if [#25](https://github.com/gamesh411/mod-playerbots/issues/25) adds a form feature. |
| Features | Full movement-era vector: duel_v5's 90-D (CF_MOVE 70-89 included - the ability head sees range/heading/band state) plus whatever [#25](https://github.com/gamesh411/mod-playerbots/issues/25)/[#26](https://github.com/gamesh411/mod-playerbots/issues/26) add. New CSV rev `duel_v6` (`ml_decisions_duel_v6.csv`). |
| Sequencing | M2 execute is blocked by #25 and #26: vector and vocab are final before the farm spins, so the stage trains once. |
| Recipe | One BC bootstrap round on `expert_action` (stock rotation tau=0) purely to initialize - a fresh multi-logit head sampling at tau=10 would be uniform-random spellbook play, the archived prior-art baseline. Then the DEC-033 exploration-first + win-anchored retrain loop, with no teacher anywhere after round 0. |
| Loop discipline | Sqrt label balance; offline degeneracy gate (state-conditional argmax) before every deploy; anti-thrash budget 2 rounds beyond the recipe, then a waiver/pivot DEC (DEC-039 discipline). |
| Capacity | Hidden 128 vs 256 sweep at the bootstrap round, picked by holdout CE + teacher agreement; the winner is fixed for the whole loop and recorded in the execute ticket. |
| Farm world | Frozen M1 canonical movement heads in both seats at movement tau=10 during farm (state diversity - user call over the tau=0 deployment-distribution default); all eval and gate runs at movement tau=0. |
| Alternation depth | 1 - ability retrain only. A movement re-adaptation round against the new ability world is the natural M3 candidate if the gate passes and movement-quality metrics shift; it is not part of M2. |
| Freeze gate | Both-seat WR uplift, delta = 0.02, vs a fresh S0-sentinel+M1-movement baseline (stock abilities on the identical movement world): epoch 2, DEC-037 rules, mixed-seat protocol, >= 2.4k matches per run, tau=0 both channels. dec033r3+M1 and dec033r3+M0 run once as report-only showcase rows in the stage card, not gate conditions. |
| Freeze contract | DEC-019 as usual: tag `stage/m2-<slug>`, `artifacts/duel/m2/{warrior,mage}.pbml` + snapshots + manifest, stage card, `duel-m2` replay-profile row (DEC-040 extension), data tag `duel_v6`. Soft-fail per DEC-028/035 precedent: record the miss, freeze the best certified artifact, no stage tag. |

**Why:**
- The spellbook head is the curriculum destination; retreating to the queue vocab would abandon it, and warm-starting imports exactly the statue-world value estimates M2 exists to shed.
- DEC-033's plateau was diagnosed as feature starvation (DEC-035); movement-active data plus the movement block gives the outcome-anchored loop its fair test, rather than re-running the teacher-imitation shape that also soft-failed.
- The BC bootstrap keeps the stock teacher out of the loop while avoiding the known-bad uniform-random cold start.
- Gating only against S0+M1 keeps the gate single and sharp: beating the stock rotation on the movement world is the claim the S-track never certified; beating a mis-calibrated statue head is near-certain and proves little.

**Consequences:** #24 closes; an Execute M2 ticket is created blocked by #25/#26, which return to the live frontier as M2 prerequisites.
Epoch-1 and superseded farm CSVs archived to `C:\AzerothCore-server\archive\csv-epoch1-stale-20260807.tar.gz` (48 files, 202M compressed, ~1.7G reclaimed); live and freeze-referenced CSVs untouched.

### DEC-043 - 2026-08-07 - duel_v6 feature extension: CF_PET + CF_FORM packs; warrior stance unlock

**Status:** accepted  
**Context:** [#25](https://github.com/gamesh411/mod-playerbots/issues/25) feature-extension ticket, unparked by the M1 freeze (DEC-041) as an M2 prerequisite (DEC-042).
The round-3 plateau handoff named three starvation candidates: pet-state (DEC-032 fog), form/stance (DEC-034 fog), and chase-state separability.
This DEC finalizes the `duel_v6` state vector before the M2 farm spins.

**Decision:**

| Piece | Rule |
|------|------|
| Chase-state | **Absorbed** - no dedicated features. `CF_DIST_NORM` x `CF_SELF_GAPCLOSE_READY` plus the CF_MOVE pack already express the conjunction; the statue-world failure was coverage (those states never occurred), which movement-active data fixes. |
| CF_PET pack (90-99) | Role-based, self/foe symmetric: `CF_{SELF,FOE}_PET_COUNT` (active controllable pets/guardians, scaled like `CF_ATTACKER_COUNT`), `_PET_HEALTH` (primary pet HP fraction, 0 when none), `_PET_KICK_READY`, `_PET_CC_READY`, `_PET_UTILITY_READY`. Role membership via per-class spell lists in `CombatDecisionFeatures.cpp` (DuelCD idiom); **non-autocast command spells only** (DEC-032 autocast exclusion carries over). Sized for future pet classes (hunter, warlock, DK, shadowfiend, feral spirits) - only count/health/CC light up for Arms vs Frost today. |
| CF_FORM pack (100-111) | Class-relative one-hot, `CF_SELF_FORM_0..5` + `CF_FOE_FORM_0..5`, all-zeros = base/no form (spec-tab idiom). Warrior: 0=Battle, 1=Defensive, 2=Berserker. Druid: 0=Bear, 1=Cat, 2=Moonkin, 3=Tree, 4=Travel. Priest: 0=Shadowform. Shaman: 0=Ghost Wolf. |
| Stance unlock | DEC-034's candidate-pool exclusion is **superseded for warrior stances**: they re-enter the M2 vocab at the duel_v6 rebuild. `MlDuelSpellPool` masks the shapeshift spell of the form the bot is already in (DEC-031 idiom), so every pickable stance is a real transition. `--drop-labels 2457 2458 71` retired for duel_v6 farms. Druid forms stay excluded (no druid seat; one-line list change when one arrives). |
| Dimensions | `CF_FEATURE_COUNT` 90 -> **112**; ability-head `ML_INPUT_DIM` 82 -> **124** (112 + 8 flags + 4 id). |
| Movement prefix slice | Frozen M1 movement heads keep their 90-D state-only input: the executor feeds `state[0..89]`, PBML artifacts byte-identical. Packs only append, so the first-90 prefix is stable forever. `duel_v6` logs all 112 state columns; the movement trainer's `FEATURE_COLS` selects the prefix, and an M3 movement re-adaptation may choose the full width on the same CSVs. |
| Data compatibility | **No zero-fill anywhere.** `duel_v6` is the only M2 ability-training data (DEC-042 fresh-farm recipe); `duel_v5` remains valid solely for the frozen movement track. Zero-filled duel_v5 rows would carry systematically false pet/form values (the elemental was frequently up while the column would read absent). |
| Bookkeeping | DEC-026's "70-D" wording needs no supersession (S2-epoch context; S2 waived per DEC-035; M2's feature contract is DEC-042's, which points here). FEATURES.md is the layout authority and gains both pack tables, marked as landing at M2 execute ([#28](https://github.com/gamesh411/mod-playerbots/issues/28)). |

**Why:**
- Pre-composed range buckets would duplicate CF_MOVE semantics (one-semantic-per-index rule) for no representational gain; a hidden layer composes the conjunction once the data covers it.
- Role bits scale to every future pet class by extending a list instead of renumbering the vector, exactly as DuelCD did for player cooldowns.
- An action whose precondition is invisible cannot be state-conditionally learned (DEC-034); the form bit makes stance picks conditionable, and same-form masking removes the always-legal no-op sink (DEC-030/031 lesson) at the source instead of in the trainer.
- Touching frozen M1 artifacts (zero-pad or retrain) buys no behavior and reopens a certified freeze; a stable prefix is free.

**Consequences:** [#25](https://github.com/gamesh411/mod-playerbots/issues/25) closes; [#26](https://github.com/gamesh411/mod-playerbots/issues/26) (Summon Water Elemental: learn vs scaffold) reaches the frontier with its prerequisite pet-state feature designed.
Implementation (feature fills, pool masks, trainer `FEATURE_COLS`/vocab changes, `ml_decisions_duel_v6.csv` logger rev) lands at M2 execute ([#28](https://github.com/gamesh411/mod-playerbots/issues/28)).
