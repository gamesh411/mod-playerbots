# Combat ML features — inventory & orthogonality

## Orthogonality rules (enforce on every addition)

1. **One semantic per index** — never reuse a float for two meanings across contexts.
2. **Packs, not soup** — group new signals into named packs (`Core`, `Class`, `DuelCD`, `DuelDR`, `DuelRange`, `FoeVitals`). Enable packs via difficulty / config masks later (D7).
3. **Stable layout within a pack** — appending new indices at the **end** of a pack is OK; renumbering requires a **new log schema version** and trainer bump.
4. **Features ≠ action flags** — state describes the world; flags describe the *candidate action*. Do not encode “I am interrupting” only in state.
5. **Heuristics may use features; features must not depend on MLP output** — no feedback loops in the vector.
6. **Document before merging code** — add a row here, then implement.

Schema / PBML input today: `ML_INPUT_DIM = CF_FEATURE_COUNT + AF_COUNT` (**70 + 8 = 78**).  
Legacy PBML1 still supported at inference via `ML_INPUT_DIM_V1 = 20` (core[0..11] + action flags only).

Duel logfile for this layout: `ml_decisions_duel_v2.csv`.

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

## Log columns (orthogonal to the net input)

| Column | Pack | Purpose |
|--------|------|---------|
| `episode_id` | Meta | Per-decision id |
| `match_id` | Meta | BG/arena instance or synthetic duel id |
| `short_reward` | Label | ~2s shaped reward |
| `terminal` | Label | +1/−1/0 match outcome |
| `reward` | Label | `short + λ*terminal` |
| `explored` | Meta | ε-greedy (arena) or duel-random policy bit |
| `in_bg` / `in_arena` / `in_duel` | Filter | Train splits |

Rotating files (`ml_decisions_duel_v1.csv` → `_v2.csv`, …) when columns / feature count change.

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
