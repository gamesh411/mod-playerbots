# Combat ML features — inventory & orthogonality

## Orthogonality rules (enforce on every addition)

1. **One semantic per index** — never reuse a float for two meanings across contexts.
2. **Packs, not soup** — group new signals into named packs (`Core`, `Class`, `DuelCD`, `DuelDR`, `DuelRange`, `FoeVitals`). Enable packs via difficulty / config masks later (D7).
3. **Stable layout within a pack** — appending new indices at the **end** of a pack is OK; renumbering requires a **new log schema version** and trainer bump.
4. **Features ≠ action flags** — state describes the world; flags describe the *candidate action*. Do not encode “I am interrupting” only in state.
5. **Heuristics may use features; features must not depend on MLP output** — no feedback loops in the vector.
6. **Document before merging code** — add a row here, then implement.

Schema / PBML input today: `ML_INPUT_DIM = CF_FEATURE_COUNT + AF_COUNT + AF_ID_COUNT` (**112 + 8 + 4 = 124**), the state having grown to 112 at M2 execute (#28, DEC-043) with CF_PET 90-99 and CF_FORM 100-111.  
Frozen M1 movement heads keep their 90-D input via the stable prefix slice `state[0..89]` (`ML_MOVE_INPUT_DIM`); packs only ever append, so that prefix never moves.  
Frozen S-track ability heads keep loading at their trained widths — **82** (`ML_INPUT_DIM_LEGACY`) and the pre-action-id **78** — against the 70-feature slice they were trained on (`CF_LEGACY_ABILITY_FEATURE_COUNT`); a PBML's input width is what selects its layout, so the `duel-s1` / `duel-s2` replay profiles score exactly as certified. Without the id pack, same-flag actions (frostbolt vs fireball) stay indistinguishable.  
Legacy PBML1 still supported at inference via `ML_INPUT_DIM_V1 = 20` (core[0..11] + action flags only).

Multi-logit heads (S2 / M2 spellbook, M1 movement) are state-only — the action is the output — so their input width is the bare feature count, **112** for a duel_v6 M2 head.

Duel logfile for this layout: `ml_decisions_duel_v6.csv`.

**Omniscience note:** foe CD / DR / spec / power are read from the live `Player*` on this private server (training signal). A human client would not see all of that; difficulty dial (D7) can later mask packs to simulate imperfect information.

---

## Pack: Core state (`CombatFeatureIndex` 0–11) — shipped

| Index | Name | Meaning | Notes |
|------:|------|---------|-------|
| 0 | `CF_SELF_HEALTH` | Self HP fraction | |
| 1 | `CF_SELF_MANA` | Primary power fraction | Rage/energy/runic mapped here when not mana — **FEAT-DEBT-001** |
| 2 | `CF_TARGET_HEALTH` | Focus / duel foe HP | Prefers duel opponent |
| 3 | `CF_TARGET_IS_PLAYER` | Target is player | |
| 4 | `CF_TARGET_IS_CASTING` | Target non-melee casting | |
| 5 | `CF_HAS_ENEMY_HEALER` | Enemy heal-cast present | Spell-agnostic |
| 6 | `CF_ENEMY_PLAYER_NEAR` | Enemy player in proximity | |
| 7 | `CF_PARTY_LOW_HEALTH` | Party member low | Weak in 1v1 duels |
| 8 | `CF_IN_BATTLEGROUND` | In BG | Arenas also set BG in AC |
| 9 | `CF_IN_ARENA` | In arena | |
| 10 | `CF_ATTACKER_COUNT` | Attackers on self (scaled) | |
| 11 | `CF_SELF_HAS_CONTROL_LOSS` | Self CC’d | |

---

## Pack: Class (12–37) — shipped (DEC-017)

Class slot order: Warrior, Paladin, Hunter, Rogue, Priest, DK, Shaman, Mage, Warlock, Druid.

| Index | Name | Meaning |
|------:|------|---------|
| 12–21 | `CF_SELF_CLASS_0`…`_9` | Self class one-hot |
| 22–24 | `CF_SELF_SPEC_0`…`_2` | Self talent tab one-hot |
| 25–34 | `CF_FOE_CLASS_0`…`_9` | Opponent class one-hot |
| 35–37 | `CF_FOE_SPEC_0`…`_2` | Opponent talent tab one-hot |

---

## Pack: DuelCD (38–48) — shipped (DEC-017)

Role readiness (`1` = known spell off CD, `0` = on CD or unknown). Class-specific spell ID lists in `CombatDecisionFeatures.cpp`.

| Index | Name | Meaning |
|------:|------|---------|
| 38 | `CF_SELF_KICK_READY` | Self interrupt ready |
| 39 | `CF_SELF_DEFENSIVE_READY` | Self major wall ready |
| 40 | `CF_SELF_OFFENSIVE_CD_READY` | Self offensive CD ready |
| 41 | `CF_SELF_GAPCLOSE_READY` | Charge / blink / etc. |
| 42 | `CF_SELF_TRINKET_READY` | PvP trinket / EMFH |
| 43 | `CF_FOE_KICK_READY` | Foe interrupt ready |
| 44 | `CF_FOE_DEFENSIVE_READY` | Foe wall ready (e.g. Ice Block) |
| 45 | `CF_FOE_OFFENSIVE_CD_READY` | Foe offensive CD |
| 46 | `CF_FOE_GAPCLOSE_READY` | Foe gap / mobility |
| 47 | `CF_FOE_ROOT_READY` | Foe root / nova ready |
| 48 | `CF_FOE_TRINKET_READY` | Foe trinket |

---

## Pack: DuelDR (49–60) — shipped (DEC-017)

Remaining DR effectiveness: level1→`1`, level2→`0.5`, level3→`0.25`, immune→`0`.

| Index | Name | Meaning |
|------:|------|---------|
| 49–53 | `CF_SELF_DR_*` | Stun / silence / root / disorient / fear on self |
| 54–58 | `CF_FOE_DR_*` | Same on foe |
| 59 | `CF_SELF_HAS_IMMUNITY` | Self IB / bubble / school immune, etc. |
| 60 | `CF_FOE_HAS_IMMUNITY` | Foe immunity aura |

---

## Pack: DuelRange (61–66) — shipped (DEC-017)

| Index | Name | Meaning |
|------:|------|---------|
| 61 | `CF_DIST_NORM` | Distance / 40y, clamped `[0,1]` |
| 62 | `CF_IN_MELEE` | Within melee range |
| 63 | `CF_IN_LOS` | Line of sight |
| 64 | `CF_SELF_FACING_FOE` | Self facing foe |
| 65 | `CF_FOE_FACING_SELF` | Foe facing self |
| 66 | `CF_BEHIND_FOE` | Self is behind foe |

---

## Pack: FoeVitals (67–69) — shipped (DEC-017)

| Index | Name | Meaning |
|------:|------|---------|
| 67 | `CF_FOE_POWER` | Foe primary power fraction |
| 68 | `CF_FOE_HAS_CONTROL_LOSS` | Foe CC’d |
| 69 | `CF_IN_DUEL` | Currently in a duel |

---

## Pack: CF_MOVE (70–89) — shipped (DEC-036 / M0 execute)

All angles foe-bearing-relative (matching the 9-way intent vocabulary); speeds normalized to base run speed.
Movement-head PBML input = **90** (state only, no action-flag / action-id packs), pinned to this prefix for good by DEC-043.
Duel logfile for this layout: `ml_decisions_duel_v5.csv`.

| Index | Name | Meaning |
|------:|------|---------|
| 70 | `CF_MOVE_SELF_SPEED_FRAC` | Current speed / base run |
| 71 | `CF_MOVE_SELF_HEADING_REL` | Velocity direction vs foe bearing |
| 72 | `CF_MOVE_FOE_SPEED_FRAC` | Foe speed / base run |
| 73 | `CF_MOVE_FOE_HEADING_REL` | Foe velocity direction vs bearing to self |
| 74 | `CF_MOVE_SELF_FACING_OFFSET` | Facing vs foe bearing (continuous) |
| 75 | `CF_MOVE_FOE_FACING_OFFSET` | Foe facing vs bearing to self |
| 76 | `CF_MOVE_CLOSING_SPEED` | d(dist)/dt, normalized |
| 77 | `CF_MOVE_SELF_AIRBORNE` | Jump state bit |
| 78 | `CF_MOVE_SELF_SNARE_FRAC` | 1 − (current/base speed) from auras |
| 79 | `CF_MOVE_SELF_ROOTED` | Self rooted |
| 80 | `CF_MOVE_FOE_SNARE_FRAC` | Foe snare fraction |
| 81 | `CF_MOVE_FOE_ROOTED` | Foe rooted |
| 82–89 | `CF_MOVE_PROBE_N`…`_NW` | 8 walkability probes (height delta + LoS at 4 y, foe-relative), shared with the executor safety clamp |

Log-only columns riding `duel_v5`: `realized_heading` (continuous heading actually executed), `movement_intent` (live movement pick), `expert_movement_intent` (M0 scripted teacher pick, DAgger-style).

---

## Pack: CF_PET (90-99) - shipped (DEC-043 / M2 execute #28)

Role-based and self/foe symmetric, like DuelCD: role membership comes from per-class spell lists in `CombatDecisionFeatures.cpp`.
Non-autocast **command** spells only (DEC-032 autocast exclusion); autocast repeats are the pet AI's job, not state.
Only count/health/CC light up for Arms vs Frost; the remaining slots are zero until a pet class enters the curriculum.
Duel logfile from this layout on: `ml_decisions_duel_v6.csv`.

| Index | Name | Meaning |
|------:|------|---------|
| 90 | `CF_SELF_PET_COUNT` | Active controllable pets/guardians, scaled (as `CF_ATTACKER_COUNT`) |
| 91 | `CF_SELF_PET_HEALTH` | Primary pet HP fraction (0 when none) |
| 92 | `CF_SELF_PET_KICK_READY` | Pet interrupt/silence command ready (e.g. Spell Lock) |
| 93 | `CF_SELF_PET_CC_READY` | Pet root/stun/incap command ready (Freeze, Gnaw, Seduction) |
| 94 | `CF_SELF_PET_UTILITY_READY` | Pet dispel/heal/other command ready (e.g. Devour Magic) |
| 95-99 | `CF_FOE_PET_*` | Same five, foe side |

---

## Pack: CF_FORM (100-111) - shipped (DEC-043 / M2 execute #28)

Class-relative one-hot (spec-tab idiom); all-zeros = base/no form.
Unlocks the DEC-034 revisit: warrior stances re-enter the M2 head with same-form re-picks masked (DEC-043).

| Index | Name | Meaning |
|------:|------|---------|
| 100-105 | `CF_SELF_FORM_0`…`_5` | Self form one-hot, class-relative |
| 106-111 | `CF_FOE_FORM_0`…`_5` | Foe form one-hot, class-relative |

Slot mapping: Warrior 0=Battle, 1=Defensive, 2=Berserker.
Druid 0=Bear, 1=Cat, 2=Moonkin, 3=Tree, 4=Travel.
Priest 0=Shadowform.
Shaman 0=Ghost Wolf.

---

## Pack: Action flags (`ActionFlagIndex`) — shipped

| Index | Name | Meaning |
|------:|------|---------|
| 0 | `AF_INTERRUPT` | Candidate is an interrupt |
| 1 | `AF_ENEMY_HEALER` | Healer-pressure / kick-heal intent |
| 2 | `AF_DEFENSIVE` | Defensive |
| 3 | `AF_CC` | Crowd control |
| 4 | `AF_HEAL` | Healing |
| 5 | `AF_INSTANT` | Instant |
| 6 | `AF_DAMAGE` | Damage pressure |
| 7 | `AF_FOCUS_PLAYER` | Focus enemy player |

Flags are **not mutually exclusive**. Appended after the 70 state features in PBML input.

---

## Pack: Action id (`AF_ID_COUNT = 4`) — shipped (S1 execute)

| Index | Name | Meaning |
|------:|------|---------|
| 0–3 | `AF_ID_*` | FNV-1a fingerprint of candidate action name (4 bytes → floats in [-1, 1]) |

Recomputed from the action name at train and inference (not logged). Separates same-flag scripted actions (frostbolt vs fireball vs attack). Must match `HeuristicScores::FillActionIdFeatures` / `tools/ml/action_flags.py`.

---

## Log columns (orthogonal to the net input)

| Column | Pack | Purpose |
|--------|------|---------|
| `episode_id` | Meta | Per-decision id |
| `match_id` | Meta | BG/arena instance or synthetic duel id |
| `action` | Label | Action taken by the live policy |
| `expert_action` | Label | Softmax-stock τ=0 pick (duel_v4 / DEC-025 DAgger) |
| `short_reward` | Label | ~2s shaped reward |
| `terminal` | Label | +1/−1/0 match outcome |
| `reward` | Label | `short + λ*terminal` |
| `explored` | Meta | ε-greedy (arena) or duel-random policy bit |
| `in_bg` / `in_arena` / `in_duel` | Filter | Train splits |
| `realized_heading` | Label (duel_v5) | Continuous heading actually executed, foe-bearing-relative radians |
| `movement_intent` | Label (duel_v5) | Live 9-way movement pick (M0: scripted intent policy) |
| `expert_movement_intent` | Label (duel_v5) | Scripted teacher pick (DAgger-style; equals `movement_intent` in M0) |

Rotating files (`ml_decisions_duel_v1.csv` → `_v2.csv`, …) when columns / feature count change. Fresh S1 DAgger farms use `ml_decisions_duel_v4.csv` (adds `expert_action`). Movement-era farms (M0+) use `ml_decisions_duel_v5.csv` (CF_MOVE 70–89 + the three movement columns). M2 farms use `ml_decisions_duel_v6.csv` (CF_PET 90-99 + CF_FORM 100-111, DEC-043); duel_v5 stays valid for the frozen movement track via the 90-D prefix.

---

## Planned (not yet features)

| Item | Notes |
|------|-------|
| Per-spell remaining CD fractions | Role bits first; finer timers if ranker plateaus |
| Difficulty pack masking | D7 — never bake difficulty into PBML weights |
| Separate rage vs mana channels | Resolves FEAT-DEBT-001 |

---

## When adding a feature checklist

- [ ] New row in the right pack table above  
- [ ] Append index (no renumber) or bump schema version  
- [ ] Update `CombatDecisionFeatures` / trainer `FEATURE_COLS` / docs together  
- [ ] Note which Directions (D#) consume it  
- [ ] Add DECISIONS entry if it changes learning semantics  
