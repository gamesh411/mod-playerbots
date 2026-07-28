# Sparring partners — hands-on S1 feel-testing

Park two **master-owned addclass** bots (Arms Warrior + Frost Mage) at a quiet duel pad away from the `MlDuelBracket` farm parks. Challenge either anytime. They use the live per-class PBML ranker with **argmax (τ=0)** and do **not** write to the farm CSV.

## Prerequisites

- `duel-farm` (or any profile with `MlDuelBracket.Enabled=1`, `ActionPolicy=ranker`, and S1 models loaded).
- `Playerbots.log` shows both models: `warrior.pbml` / `mage.pbml` with `in=82`.
- You are logged in on a **real** character (same faction as the bots you add).

Do **not** duel farm rndbots at the Elwynn/Durotar parks for feel-testing — that is farm traffic.

## In-game setup

1. Summon partners (once per session, or leave them online):

```
.bot addclass warrior
.bot addclass mage
```

2. Confirm specs are **Arms** and **Frost** (addclass pulls from the addclass pool; under duel-farm that pool is Arms/Frost-heavy). Respec or re-add if needed.

3. Teleport yourself, then each bot, to the quiet pad for your faction (GM `.go xyz` / `.bot` follow / MultiBot teleport — whatever you normally use).

| Faction | Quiet pad (map,x,y,z,o) | Notes |
|---------|-------------------------|--------|
| Alliance | `0, -9470, -1290, 41.5, 0.5` | Eastvale Logging Camp area — south-east of farm park `-9120,355` |
| Horde | `1, 990, -4550, 12.5, 0.5` | South of farm park `1318,-4386` in Durotar |

Park both bots a few yards apart at the same pad so you can pick either.

4. Challenge a bot with a normal duel request. It auto-accepts (`duel` strategy; master skips the farm resource gate).

5. Rematch as needed. After each duel the bracket restore path may top them up when enabled.

## Behaviour contract

| Concern | Farm rndbots | Sparring addclass |
|---------|--------------|-------------------|
| Matchmaking | Auto Arms↔Frost | None (you challenge) |
| ForceToPark | Yes → farm parks | No (not random bots) |
| Softmax τ | Conf (usually 10) | **0** (argmax) vs real player |
| CSV / `duel_v4` | Logged | **Skipped** when either side is a real player |

## Orchestrator pointer

Printable checklist: `wotlk-playerbots-server/scripts/sparring-partners.ps1`.
