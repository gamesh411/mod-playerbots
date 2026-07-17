# S1 training lessons — novel failure modes (DEC-025 / #18)

This note is for **learning**, not a changelog. It records non-planned problems that showed up while executing S1 (scripted-vocab scalar ranker) on Arms Warrior vs Frost Mage duel RL in mod-playerbots — what broke, why it breaks in general, how you notice it, and when to suspect the same pattern again.

---

## Setup snapshot

| Piece | Value |
|-------|--------|
| Curriculum | S0 Softmax-stock → **S1** scalar ranker over scripted queue → S2 spellbook (DEC-018 / 025 / 026) |
| Matchup | Arms Warrior vs Frost Mage (movement stays scripted) |
| Freeze gate | Mixed seats beat stock↔stock by **δ = 0.02** |
| Mixed seats | Arms-ranker↔Frost-stock **and** Arms-stock↔Frost-ranker |
| Stock baseline (v3) | Warrior **74.6%** / Mage **25.8%** |
| Ranker↔ranker | Diagnostic only — not the freeze claim |

**Status at write time:** after the 82-D DAgger round with fixed expert elevation, Frost-ranker mixed eval reached Mage **31.0%** (+5.2pp vs stock, **PASS**) on ~2.4k matches. Arms-ranker fresh eval may still be pending in sibling work.

Frost vs Arms is mostly about **precise ability sequencing** (Frostbolt → Ice Lance windows, Nova / Deep Freeze, elemental Nova) more than coarse scripted-queue *categories*. That intuition foreshadows why representation bugs hurt the mage seat hardest, and why S2 spell-id multi-logit (DEC-026) is the next curriculum step if S1 ever plateaus below freeze after representation + DAgger are healthy.

---

## 1. Losing-seat reward asymmetry / expert elevation bug (DAgger)

**Failure.** Mage on-policy rows are ~86% losses (mean reward ≈ −17, p50 = −25 with `TerminalLambda=25`). Warrior is ~91% wins and learns easily. Old `--imitate-expert` only added a score-up example when `expert != action`. When the learner already matched the Softmax-stock expert *and then lost*, that same action kept target −λ. Imitation never pulled the teacher pick up to stock; expert-off afterward washed imitation further (mage ~6–9% WR).

**Why.** DAgger wants “on learner states, prefer the teacher’s pick.” On a skewed seat, most matched expert rows are *losses*. If you only elevate when the learner *disagreed*, you never rescue the teacher action from the loss label — you actively reinforce “do what stock would do → get −λ.”

**Signal.** Mage WR collapses below stock during/after DAgger; warrior looks fine. Many rows where `action == expert_action` still have strongly negative `reward`.

**Fix.** Always elevate Softmax-stock expert to `imitate_target` (including `learner == expert`): overwrite the matched row’s target, or append a separate elevated expert example when they differ (`train_ranker.py`).

**Lesson.** On imbalanced / winrate-skewed seats, **imitation and reward fight**. Do not run expert-off until the learner is near the teacher. Suspect this whenever one class wins easily and the other sits at 10–30% WR under the same train recipe.

---

## 2. Action-flag collision / identity collapse (representation)

**Failure.** `ScoreDuel` input was 70 state + 8 role flags. Frostbolt, fireball, frostfire bolt, attack anything, and attack duel opponent shared identical flags → identical scores (200/200 offline). Frost Nova == Deep Freeze similarly. Softmax τ=0 argmax among ties plus category collapse favored ice block / icy veins over the damage bucket → ~5–9% WR. Softmax-stock still worked because heuristic *relevance* differentiates spells; the learned scorer could not.

**Why.** A ranker can only prefer A over B if the input for A and B differs (or the head has per-action parameters). Role flags are intentional *aliases* for combat roles — they are not identities.

**Signal.** Offline: dump scores for same-state candidates; many distinct names, same float. Online: WR far below stock despite “training succeeding” (MSE can look OK while ranking is useless).

**Fix.** FNV-1a action-name fingerprint → +4 dims → `input_dim` 82. Frostbolt ≠ fireball; mage train MSE dropped ~393 → ~181. See `FEATURES.md` / `HeuristicScores::FillActionIdFeatures`.

**Lesson.** If action encoding aliases distinct actions, ranking cannot imitate or improve. **Check offline score equality** before more farm/train loops. Same class of bug awaits S2 if spell ids collapse or legality masks hide true choice.

---

## 3. Logger schema / mid-file width downgrade (data plumbing)

**Failure.** Headerless duel_v4 is **90** cols (includes `expert_action`); v3 is **89**. On restart, detection used substring `expert_action` on the first line — data rows do not contain that string → writer silently switched to 89-col mid-file. The `terminal` column shifted; eval saw ~142 matches then frozen growth while the CSV kept growing.

**Why.** Append-only logs are a contract: column *position* is the schema when there is no header. A probe that only works for headered files will misclassify headerless v4 as v3.

**Signal.** Match count in the eval harness stalls while file size grows; parse of `terminal` / rewards looks nonsense; column histograms jump after a process restart.

**Fix.** For headerless files, detect v4 by column count (≥90). Prefer headered CSVs when practical (`MlDecisionLogger.cpp`).

**Lesson.** Never change width mid-file. Validate column counts over time. Headered files are safer for evolving schemas.

---

## 4. Thin-sample false hope

**Failure.** Early 142-match frost eval showed 26.8% (+0.9pp) after the 82-D fix — looked near the gate. A clean thicker sample (~2.4k) showed 20.7% (−5.2pp). A later DAgger round recovered to **31.0%** (+5.2pp PASS).

**Why.** Binomial WR variance at n≈100–200 is several percentage points; logging bugs (problem 3) can also inflate a “lucky” thin window.

**Signal.** Gate-adjacent WR on hundreds of matches, especially right after a logger or deploy change.

**Lesson.** Do not freeze or pivot curriculum on n ≪ thousands for duel WRs. Treat thin n as a smoke test only.

---

## 5. Ops: rebuild vs running worldserver

**Failure.** `cmake --install` / `ensure_running` reported build failure while ninja had already linked — the exe was locked by a running worldserver.

**Why.** On Windows, replacing a running binary fails; install tools often surface that as a generic build error.

**Signal.** Fresh object files / linker success in the build log, but install fails; binary mtime unchanged; worldserver still running.

**Lesson.** Stop servers before install. Do not trust “build failed” without reading `cmake-build.log` vs binary mtime.

---

## 6. Mixed-seat Softmax-stock fallback (earlier in #18)

**Failure.** Empty per-class PBML under `ActionPolicy=ranker` fell through to `queue.Peek()` instead of Softmax-stock τ=0. That broke the DEC-025 freeze definition (stock seat must match the baseline policy).

**Why.** “No model” is ambiguous: peek-default vs intentional stock seat for mixed eval.

**Signal.** Mixed-seat WRs that do not line up with stock↔stock baseline even before the ranker is strong; Engine path takes Peek when a model path is cleared.

**Fix.** Engine: ranker policy + missing PBML → Softmax-stock (same as S0 baseline).

**Lesson.** In mixed eval, the stock seat must be the **same policy** as the published baseline — same τ, same Softmax-stock path.

---

## Debugging checklist (future duel ML)

1. **Representation first** — same-state offline scores: do distinct actions differ?
2. **Schema integrity** — column count stable across the whole CSV; `terminal` parseable; match counts track file growth.
3. **Seat asymmetry** — if one class is ≪50% WR, inspect imitation vs reward on `action == expert_action` rows before expert-off.
4. **Sample size** — treat n≪~1–2k duel matches as smoke only for δ=0.02 gates.
5. **Deploy honesty** — servers stopped for install; binary mtime matches the build you think you shipped.
6. **Mixed-seat contract** — empty PBML path = Softmax-stock τ=0, never Peek-by-accident.
7. **Plateau vs bug** — if WR is stuck *after* (1)–(6) look healthy, that is a curriculum signal toward S2 — not another blind retrain.

---

## Open questions / S2 foreshadow

- **Combo precision:** scripted queue categories are coarse; Frost’s win condition is sequencing and windows. S1 can still pass mixed seats (frost already did at 31.0%), but a true plateau below freeze after representation + DAgger would correctly push to **S2 spell-id multi-logit** (DEC-026).
- **Water Elemental Frost Nova:** may need cast + ground click. Unclear whether queue/spellbook paths are ready — flag as an **S2 execute risk / open question**, not something S1 can honestly solve.
- **Movement/kiting:** matters in human play; out of map scope here (stay scripted). Do not chase kite bugs inside the ranker.

Successful recovery path this round: fix identity (82-D) → fix expert elevation → thick-sample eval → frost PASS; keep arms eval honest before freeze.

---

## Pointers

| Doc / code | Role |
|------------|------|
| [DEC-025](../DECISIONS.md) | S1 train / deploy / freeze contract |
| [s1-scripted-vocab-ranker.md](../curriculum/s1-scripted-vocab-ranker.md) | Stage card |
| [FEATURES.md](../FEATURES.md) | 70 + 8 + 4 input; `expert_action` / duel_v4 |
| `tools/ml/train_ranker.py` | DAgger score-up / `imitate_target` |
| `HeuristicScores` + action-id fill | Role flags + FNV fingerprint |
| `MlDecisionLogger.*` | Append-only width / v3 vs v4 |
| `Engine.cpp` | Softmax-stock fallback for mixed seats |
| `artifacts/duel/s1/README.md` | Live PBML / freeze eval numbers |
