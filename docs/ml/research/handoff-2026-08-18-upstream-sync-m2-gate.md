# Handoff - upstream sync verified + M2 round-2 gate measured (2026-08-18)

For a new agent session. Read after **DEC-042** (M2 design) and the round-2 comment on [#28](https://github.com/gamesh411/mod-playerbots/issues/28).

Two things happened in this session: core + all modules were rebased onto upstream, and the M2 round-2 heads got their first win-rate measurement.
The gate result is a curriculum finding, not sync damage - the evidence for that separation is below and is worth not re-deriving.

## Where we are

- **Map:** [#4](https://github.com/gamesh411/mod-playerbots/issues/4)
- **Ticket:** [#28](https://github.com/gamesh411/mod-playerbots/issues/28) - M2 execute **in progress**; round 3 **wired but deliberately not launched**
- **Branch:** `exp/duel-rl-curriculum` @ `589e9bd5`, tagged `post-upstream-sync-20260816`
- **Orchestrator:** `wotlk-playerbots-server` `9fe76a6` (pushed)

## M2 round-2 gate - both seats FAIL

Full DEC-042 protocol: mixed seat, tau=0 both channels, delta 0.02, DEC-037 rules, fresh S0-sentinel+M1 baseline.
450 bots at 96.4% duel saturation; every leg cleared the >=2.4k match minimum several times over.

| Run | Seat | WR | Matches | Threshold | Verdict |
|-----|------|----|---------|-----------|---------|
| Baseline: S0 sentinel + M1 movement | warrior | 45.9% | 10,709 | - | - |
| Baseline: S0 sentinel + M1 movement | mage | 54.6% | 10,645 | - | - |
| r2 arms-ranker | warrior | **46.9%** | 5,308 | >=47.9% | **FAIL** - +1.0pp, ~1.2 sigma (parity) |
| r2 frost-ranker | mage | **35.8%** | 7,875 | >=56.6% | **FAIL** - -18.8pp, ~26 sigma |

The baseline is fresh per DEC-042 and is **not** comparable to M1's 39.3/61.5 M0<->M0 figure, which predates DEC-044 (pet reset, 25% unglyphed mages).
The 39.3 -> 45.9 and 61.5 -> 54.6 shift is that engineering, measured.

This confirms the starvation worry raised in the round-2 comment (won_frac 0.718/0.242).
The mage seat is not flat, it is far below stock. The warrior lands at parity, the same shape M1 hit in DEC-041.

The head is **not** degenerate: 32.7% of duels contain a Summon Water Elemental cast at tau=0, 53 distinct argmax on gate states.
It is simply a weaker policy than the tuned stock rotation - 12.4% of its decisions go to wand shoot and 7.3% to Counterspell, against a baseline of 21.1% frostbolt / 8.8% frost nova / 8.0% deep freeze.

### Two measurement traps worth not stepping in again

1. **The per-deploy degeneracy gate cannot detect a weak policy.** It asks "is this action dead", never "is this policy good". Both round-2 heads passed it and then measured -18.8pp / +1.0pp.
2. **The DEC-050 pet-down floor is a farm-distribution metric.** The identical mage head reads **43.0%** on tau=10 farm states and **0.1%** on tau=0 gate states. Do not read it on gate data; it will look like a dead action when the log shows the spell being cast in a third of duels.

## Why the gate result is not sync damage

Recorded so a future session does not have to re-run these controls:

| Control | Result |
|---------|--------|
| `eval_duel_winrate.py` on M1's archived freeze-gate CSV | **39.3% / 61.4%** vs the manifest's documented 39.3% / 61.5% |
| Clone movement head vs post-sync data | teacher agreement **0.9828**, all four bands correct |
| M2 warrior head on the gate | parity - a broken feature vector would not spare one seat |
| DEC-050 pet-down floor at matched tau=10 | **43.0%** pre-sync vs **39.7%** post-sync |
| Baseline stock rotation composition | textbook Frost (frostbolt 21.1%, frost nova 8.8%, deep freeze 8.0%) |

Runtime change the sync forced: upstream removed `PlayerbotAI::IsRealPlayer()` and reused the name for a free function meaning "no bot AI at all".
Seven ML call sites migrated in `49b37aa2`; `MlDuelBracket.cpp` keeps `IsRealPlayerParticipant`, which must stay `IsRealPlayer(p) || IsSelfBot(p)` because a selfbot is a human at the keyboard and the removed method tested exactly that.

## Data layout after cleanup

Live under `C:\AzerothCore-server\` - the M2 loop aggregates every prior round on retrain, so these stay uncompressed:

`ml_decisions_duel_m2_r{0,1,1_boot1,2_boot1,2}.csv` plus this session's six `*_m2gate_*.csv`.

Archived to `C:\AzerothCore-server\archive\` (see the README there; every tarball was member-count verified before originals were removed):

| Archive | Source -> compressed |
|---------|---------------------|
| `m1-frozen-20260817.tar.gz` | 11.41 -> 1.34 GB, originals removed |
| `s-track-frozen-20260817.tar.gz` | 2.95 -> 0.39 GB, originals removed |
| `m2-active-20260817.tar.gz` | 4.69 -> 0.64 GB, **backup copy, originals live** |
| `m2-gate-20260817.tar.gz` | 0.91 -> 0.07 GB, **backup copy, originals live** |

**Movement log rotated** `ml_movement_duel_v1.csv` -> `ml_movement_duel_v2.csv` (`config.ps1` `9fe76a6`).
v1 is M1's frozen `data_tag` and had reached 8.6 GB by accumulating M2-era rows behind that name.
Rows after line **34,768,251** of the archived v1 are post-sync verification data, not M1 training data.
`ml_decisions_duel_m2_r2.csv` likewise carries verification rows after line **2,842,275**; those ran under the round-2 farm config so round-3 aggregation can keep them.

## Open decisions

1. **Round 3 go/no-go.** It is wired (`ml_decisions_duel_m2_r3.csv`, round-2 heads deployed) and intentionally not started. Given round 2 measures -18.8pp on the mage seat, decide first whether more win-anchored data on a world the warrior dominates fixes the starvation or compounds it. Anti-thrash budget is still intact at 2 - this session spent none of it, since a gate measurement is not a round rerun.
2. **A DEC for the gate result.** Not written. Both seats missed, which is DEC-028/DEC-035 soft-fail territory, but the loop is mid-flight so it may be premature.
3. **`exp/duel-rl-curriculum` force-push.** The rebase rewrote history, so the branch pointer on `fork` still sits at the pre-rebase `ce520262`. Both `pre-upstream-sync-20260816` and `post-upstream-sync-20260816` tags **are** pushed, so no work is at risk - the full rebased history is on the remote via the post-sync tag. Only the branch pointer move is outstanding, and it needs `--force-with-lease`.

## Ops notes

- `C:\AzerothCore` has **no remote for our commits** - `origin` is upstream. Its downstream work exists only in that checkout plus the local tags. The pre-sync git bundles were deleted once the gate verified the sync, and `data\mmaps.v19` (the pre-`MMAP_VERSION` 20 tile set) was deleted with them. See `C:\AzerothCore-backup-20260816\RESTORE.md`.
- Reproduce any gate leg with the scratch runner pattern: set `MlDuelBracket.SoftmaxTemperature=0`, `MlDuelMovement.SoftmaxTemperature=0`, `StockSoftmaxTemperature=0` for mixed legs, clear the stock seat's `MlModelPathDuel.<Class>`, point both logs at run-specific CSVs, then run `worldserver.exe` with stdin held open and close it for graceful shutdown. A PowerShell pipeline cannot do this - it buffers the left side to completion before the native process starts.
- `ensure_running.ps1 -ProfileGB 32 -ServerProfile duel-farm -SkipPrereqs -SkipBuild -SkipExtract -NoStart` rewrites the confs back to the farm state after gate runs. It has been run; the live conf is round-3 farm, not gate.
