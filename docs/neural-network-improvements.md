# How Neural Networks Would Improve Current Bot Capabilities

This document explains **what today’s heuristic Option B/C cannot do well**, **how specific neural network designs would fix those gaps**, and **concrete in-game examples** tied to the existing Strategy / Multiplier / Action pipeline.

Current baseline (already in tree):

- Feature Value: `combat decision features` (`src/Ai/Ml/CombatDecisionFeatures.*`)
- Option B: `HybridRelevanceMultiplier` — fixed if/else relevance scales
- Option C: `PvpPolicyMultiplier` — stronger fixed scales for BG/arena

Neural nets would **replace or blend with** those fixed multipliers. They would **not** bypass `isUseful` / `isPossible` / Action execution.

---

## 1. What Heuristics Get Wrong (Why NNs Help)

Today’s scorers use **independent boolean rules** with constant multipliers, e.g.:

| Heuristic rule | Fixed effect |
|---|---|
| Target casting AND action is interrupt | ×1.35 (B) / ×1.40–1.55 (C) |
| Enemy healer casting AND `on enemy healer` | ×1.45–1.65 |
| Self low AND defensive | ×1.40–1.50 |
| Party low AND heal | ×1.25 |

### Failure modes this creates

1. **No interaction effects** — “kick healer” and “trinket” and “defensive” compete with flat numbers; the net cannot learn “trinket *then* kick” sequencing as a pattern over time.
2. **No class/matchup awareness** — same boost whether the caster is a holy paladin (must stop) or a warrior Heroic Throw (ignore).
3. **No cooldown / DR / diminishing returns** — heuristics don’t know kick is on CD, Poly DR is purple, or Bubble is up.
4. **No temporal context** — cannot remember “we already burned CC; now go for the kill.”
5. **No continuous judgment** — health 31% vs 29% is a cliff at `criticalHealth`; humans blend smoothly.
6. **Name-substring classification is brittle** — `IsInterruptAction("kick")` works; exotic or renamed actions may be missed.

Neural nets address these by learning a **non-linear function**  
`score(action | rich_state)` from logged good play (imitation) or outcomes (RL).

---

## 2. Concrete Upgrade Paths (NN Implementations)

### NN-1 — Action Relevance Ranker (drop-in for Option B)

**Role:** Replace `HybridRelevanceMultiplier::GetValue` with a tiny network.

**Architecture (recommended first NN):**

```
Input:  feature vector F (expand from 12 → ~64–128 floats)
        + action embedding A (one-hot or learned ID for ~80–200 legal actions)
Hidden: MLP 128 → 64 → 32  (ReLU)
Output: scalar relevance multiplier in [0.3, 1.9]   OR
        pairwise preference P(prefer action_i over action_j)
```

**Training:**

- Log each tick: `(F, candidate_actions[], chosen_action, outcome_3s)`
- Imitation: behavior cloning from high-rated human/bot logs (“what did the winner pick?”)
- Or listwise ranking loss (LambdaMART-style / softmax over candidates)

**Runtime:** ONNX on CPU, ≤100 µs; still multiplied onto existing relevance; Action gates unchanged.

#### In-game example — 2v2: Rogue + Arms vs Resto + Mage

| Moment | Heuristic today | NN ranker improvement |
|---|---|---|
| Mage starts Polymorph on your resto partner; healer also starts a heal | Both “kick” and “blind on healer” get flat boosts; may Blind first and eat Poly | Learns: **kick mage Poly first** when partner is unprotected and Poly cast time remaining &lt; 0.6s |
| Healer is at 20%, you have Kidney ready, partner has no DR | Flat “CC healer” boost | Learns: **Kidney → burst**, not random Blind, when kill window features fire |
| You are 25% HP, Cloak ready, enemy has DoTs | Always boosts Cloak when low | Learns: Cloak only if **magic damage incoming**; otherwise Evasion / trinket |

**Possible implementation hook:**

```cpp
float HybridRelevanceMultiplier::GetValue(Action* action)
{
    if (OnnxRanker::Ready())
        return OnnxRanker::Score(AI_VALUE(...features...), action->getName());
    return HeuristicScore(action); // current code as fallback
}
```

---

### NN-2 — Arena / BG Policy Net (drop-in for Option C)

**Role:** Stronger than a ranker: outputs a **preferred action distribution** among legal Actions for BG/arena bots only.

**Architecture:**

```
Input:  observation o_t
  - F (combat features, expanded)
  - per-unit slots (self, partner, enemy1, enemy2): class one-hot, HP, mana,
    cast spell-id embedding, major CD bitset, CC DR tiers, distance, LoS
  - optional 4–8 frame stack OR GRU/LSTM over last 0.5–1.0s
Output: softmax over masked action IDs (illegal actions → -inf)
        optional aux heads: target_id, move_intent (strafe L/R/back)
```

**Training:**

- Offline RL (CQL / IQL) or imitation from arena replays
- Reward: win/loss, damage on healers, interrupts landed, deaths prevented

**Runtime:** 0.2–1.0 ms on i7-class CPU for &lt;1M params; **only** for `InBattleground() \|\| InArena()`.

#### In-game example — WSG midfight

| Moment | Heuristic today | Policy net |
|---|---|---|
| Enemy FC at 40yd, local skirmish at 10yd | Flag action ×1.25 always | Learns **when** to peel for FC vs finish local kill based on ticket/time/FC HP |
| You are hunter; warrior closing to 8yd | Soft “instant preferred” | Outputs **Disengage + Freezing Trap** sequence intent mapped to Actions |
| Node fight in AB; 3 enemies incoming | Flat player-target bias | Learns to **kite to node / call peel** vs greed damage |

#### In-game example — 3v3 Double DPS + Healer

| Moment | Heuristic | Policy net |
|---|---|---|
| Swap call: enemy rogue vanished | No concept of vanish timers | From history features: **pre-spreads / trinket hold / stop casting** |
| Your healer deep frozen | Boosts any CC/dispel | Prefers **specific** freedom/dispel Action that is off CD and in range |

**Implementation shape:**

```cpp
// Inside Engine::DoNextAction, after candidates are known (optional fast path):
if (bot->InArena() && PvpPolicyNet::Ready())
{
    Action* preferred = PvpPolicyNet::Pick(features, legalActions);
    // raise preferred node's relevance, still run isUseful/isPossible
}
```

Or keep multiplier form: policy net emits per-action scores used like today’s `PvpPolicyMultiplier`.

---

### NN-3 — Interrupt / Kick Timing Head (shared B+C, also huge for PvE)

**Role:** Specialize one hard skill heuristics botch: **which cast to stop, and when**.

**Architecture:** small classifier / regressor

```
Input:  for each nearby enemy cast bar:
          spell_id embed, remaining_ms, school, is_heal, target_is_friend,
          my_interrupt_cd, range, LoS
Output: interrupt_now?  + which_unit_guid  + which_interrupt_spell
```

#### Examples

| Fight | Heuristic | NN |
|---|---|---|
| Arena: mage Pyroblast vs healer Flash Heal overlapping | Boosts interrupt generically; may kick Pyro | Learns **kick Flash Heal** if kill pressure is up; kick Pyro if self/partner would die |
| Kara / dungeon: Aran / caster packs | Same | Learns priority list: **CC break heals > raid damage casts > filler** |
| ToC Anub’arak / ICC caster adds | Flat | Learns boss-script-like priorities from logs without hand scripts |

This single head alone can raise both PvP winrate and PvE interrupt reliability.

---

### NN-4 — Healer Triage Net (Option B global — PvE + PvP)

**Role:** Rank heal targets / spell choice better than `party low health` scalar.

**Architecture:**

```
Input:  party members[0..4]: hp, incoming_dps_est, hots, CDs, distance, LoS, role
Output: target_index + heal_spell_bucket (instant / efficient / big)
```

#### Examples

| Situation | Heuristic | NN |
|---|---|---|
| Tank 60%, DPS 25% with trinket, other DPS 40% | “party low” boosts all heals | Heals **25% DPS** unless tank spike predicted |
| Arena: partner 35% stunned, you 50% | Flat defensive/heal boost | Prefers **HoP / PS / external** over self-heal when partner is the kill target |
| Raid: two people in void zone | Same | Learns **不动 (don’t) hard-cast**; instant only — overlaps with cast-time strategy but finer |

---

### NN-5 — Target Swap / Kill Window Net

**Role:** Decide **who** to hit, not just how hard to boost `attack enemy player`.

**Architecture:** scoring head over enemy units

```
score(unit) = f(hp, class, defensives_up, healer_external, DR, distance, team_focus)
pick argmax among enemies in range
```

#### Examples

| Situation | Heuristic | NN |
|---|---|---|
| Arms warrior 15% with Die by the Sword + healer 40% free casting | Still tunnels warrior if current target | Swaps to **healer** until DBTS ends |
| Mage Ice Blocked; hunter 50% | May sit on Block | Instantly swaps |
| BG: flag carrier 80% escorted by 3; free 20% priest alone | Flag ×1.25 may greed FC | Learns **pick priest then reassess FC** unless cap is imminent |

Maps to Actions: set current target / `attack enemy player` / class focus macros.

---

### NN-6 — Movement Micro Net (Option C only; hardest)

**Role:** Output movement intents — the biggest “feels human” gap in arenas.

**Architecture:**

```
Input: positions, velocities, enemy charge/gap-closer CDs, pillar map embedding
Output: discrete move: forward/back/strafe/jump/sit_los  OR small continuous offset
```

Must still go through existing MovementActions / pathfinding — NN proposes, physics validates.

#### Examples

| Situation | Today | NN |
|---|---|---|
| Mage line-of-sighting Poly behind pillar | Arena tactics / scripts limited | Strafe to break LoS then re-peek |
| Warrior about to Charge | Rarely anticipates | Pre-move out of Charge arc when Charge CD known |
| WSG juke near GY ramp | Waypoint paths | Feint path that heuristics don’t encode |

**Caution:** highest risk of stuck/spinning; ship last; shadow-mode first.

---

### NN-7 — Spell / CD Embedding Tower (foundation model for all heads)

**Role:** Not a decision maker alone — learns dense vectors for spell IDs and auras so other nets generalize.

```
spell_id → 16–32 dim embedding (trained with all heads)
```

Lets the interrupt head treat “any 2.5s heal cast” similarly across classes without substring hacks like `IsHealActionName`.

---

## 3. Side-by-Side: Same Fight, Heuristic vs NN

### Scenario: Resto Druid in 2v2, partner Rogue; enemies Warlock + Resto Shaman

**t=0:** Shaman starts Healing Wave; Lock starts Fear on you.

| System | Decision |
|---|---|
| **Today B+C** | Boosts interrupts and defensives flatly; may Barkskin and also try bash; order depends on queue relevance numbers |
| **NN-1+3+4** | Predicts Fear lands first → **Barkskin or trinket**, then **Typhoon/Bash shaman heal**, hold Cyclone for after DR; if partner Kidney ready on shaman, **skip Bash** and let Kidney set up |

**t=3s:** You are 28%, partner 55%, shaman 22% after Kidney.

| System | Decision |
|---|---|
| **Today** | Self-heal boost (selfLow) may greed Regrowth cast |
| **NN** | Instant **NS Heal** or **Nature’s Swiftness** on self only if incoming &gt; X; else **keep pressure / Cyclone lock** because kill window is open |

That difference is exactly “rated player” vs “scripted bot.”

---

## 4. Feature Expansion Required for Real NN Gains

Current 12 floats are enough for heuristics; NNs need more. Suggested expansion (still CPU-cheap):

| Block | Examples | Why |
|---|---|---|
| Self CDs | kick_cd, trinket_cd, major_def_cd | Avoid boosting kick when on CD |
| Target cast | spell_id embed, remain_ms, school | Interrupt priority |
| Enemy units (×3) | class, hp, distance, DR, immunities | Swap / CC choice |
| Partner | hp, cc’d?, externals | Peel decisions |
| Match | arena team size, BG type, time | Context |
| History | last 4 actions, last 2s damage taken | Sequencing |

Keep total input ≤ ~128–256 floats for ≤100 µs–1 ms inference.

---

## 5. Implementation Roadmap (Practical)

| Stage | Deliverable | Replaces |
|---|---|---|
| **0** | Logging: `(features, legal actions, chosen, reward)` | — |
| **1** | Expand features + spell embeddings | Input quality |
| **2** | **NN-1** ONNX ranker behind `HybridRelevanceMultiplier` | Option B heuristics |
| **3** | **NN-3** interrupt head (shared) | Part of B/C |
| **4** | **NN-2** arena policy net behind `PvpPolicyMultiplier` | Option C heuristics |
| **5** | **NN-4/5** heal triage + kill target | Cross PvE/PvP |
| **6** | **NN-6** movement (optional) | Arena feel |

**Always:** heuristic fallback if model missing / inference timeout / confidence low.

```
score = α * nn_score + (1-α) * heuristic_score
α → 1.0 after validation
```

---

## 6. What NNs Will Not Magically Fix

- Missing Actions / spells not implemented in class AI
- Wrong pathfinding / BG objective scripts (still need WarsongStrategy etc.)
- Server tick / GCD / react delay floors
- Raid boss scripting that needs precise positioning timelines (hand strategies remain king for scripted encounters unless you train per-boss specialists)

NNs **amplify** decision quality among **legal** Actions. They do not invent new game abilities.

---

## 7. Capability Gain Summary

| Capability | Now (heuristics) | With NNs |
|---|---|---|
| Interrupt choice | “Something is casting → boost kick” | “Kick *this* cast at *this* time” |
| Healer focus | Flat boost if healer casting | Matchup- and DR-aware lockdown plans |
| Defensive use | HP threshold cliffs | Anticipatory based on incoming + CDs |
| Target swap | Prefer players / FC | Real kill-window swaps |
| Heal triage | Party-low scalar | Role/incoming-aware targeting |
| Arena micro | Weak | Policy + optional movement net |
| PvE transfer | Interrupt/heal boosts help | Same heads fire on dungeon casters/raid heal triage |
| Adaptivity | Edit C++ constants | Retrain from logs |

**Bottom line:** Neural nets improve the bots by learning **context-sensitive tradeoffs** (what to do *given this matchup, these CDs, this second of the fight*) that fixed multipliers cannot express. The highest-ROI first implementations are **NN-1 (relevance ranker)** and **NN-3 (interrupt head)** on the existing feature → Multiplier path, then **NN-2 (arena policy)** for Option C.
