#!/usr/bin/env python3
"""Offline degeneracy check for S2 multi-logit PBML: argmax distribution over sampled states."""

import argparse
import collections
import csv
import sys
from pathlib import Path

import numpy as np

CLASS_F = {"warrior": 12, "mage": 19}
# CF_SELF_PET_COUNT, first column of the duel_v6 CF_PET pack (DEC-043).
SELF_PET_COUNT_F = 90
# The trainers keep match_id % 10 == 7 out of the optimizer; a gate scored on trained-on states
# measures memorisation, so this one reads the same holdout.
HOLDOUT_MOD = 10
HOLDOUT_REMAINDER = 7


def load_pbml_multi(path: Path):
    toks = path.read_text().split()
    i = 0
    assert toks[i] == "PBML1"
    i += 1
    d = {}
    while toks[i] in ("input_dim", "hidden_dim", "output_dim", "vocab"):
        key = toks[i]
        if key == "vocab":
            n = int(toks[i + 1])
            d["vocab"] = [int(t) for t in toks[i + 2 : i + 2 + n]]
            i += 2 + n
        else:
            d[key] = int(toks[i + 1])
            i += 2
    def block(name, count):
        nonlocal i
        assert toks[i] == name, (name, toks[i])
        i += 1
        vals = np.asarray([float(t) for t in toks[i : i + count]], dtype=np.float32)
        i += count
        return vals
    h, n_in, n_out = d["hidden_dim"], d["input_dim"], d["output_dim"]
    d["w1"] = block("W1", h * n_in).reshape(h, n_in)
    d["b1"] = block("b1", h)
    d["w2"] = block("W2", n_out * h).reshape(n_out, h)
    d["b2"] = block("b2", n_out)
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pbml", type=Path, required=True)
    ap.add_argument("--csv", type=Path, required=True)
    ap.add_argument("--self-class", choices=list(CLASS_F), required=True)
    ap.add_argument("--max-states", type=int, default=20000)
    ap.add_argument("--holdout-only", action=argparse.BooleanOptionalAction, default=True,
                    help="score only matches the trainers held out (match_id %% 10 == 7). "
                         "--no-holdout-only reads every match, for CSVs the head never trained on")
    ap.add_argument("--exclude", type=int, nargs="*", default=[],
                    help="spell ids masked at runtime by MlDuelSpellPool (toggles, stances); "
                         "their logits are set to -inf before argmax to mirror live behavior")
    ap.add_argument("--summon-spell", type=int, default=31687,
                    help="DEC-044 starvation-floor subject (Summon Water Elemental)")
    ap.add_argument("--pet-down-floor", type=float, default=None,
                    help="DEC-044: minimum argmax share of --summon-spell over pet-down "
                         "summon-ready states. Default 0.10 on duel_v6 CSVs, skipped on older "
                         "revisions that have no CF_PET pack; pass a value to require the check "
                         "(or 0 to disable it)")
    args = ap.parse_args()

    m = load_pbml_multi(args.pbml)
    cf = f"f{CLASS_F[args.self_class]}"
    pet_f = f"f{SELF_PET_COUNT_F}"
    xs = []
    experts = []
    kept_rows = []
    # Two streaming passes instead of materializing the log: a farm CSV held as row dicts costs
    # roughly 6 KB per row - tens of GB for a duel_v6 file - which OOMs the box out from under a
    # running farm (same trade the spellbook trainer makes).
    with args.csv.open(newline="", encoding="utf-8", errors="replace") as f:
        reader = csv.DictReader(f)
        has_pet_pack = pet_f in (reader.fieldnames or [])
        # Summon readiness is not in the state vector, so approximate the 3-min cooldown by the
        # bot's own first summon inside the duel: before it the spell is up (DEC-037 clears
        # cooldowns at rematch), after it the state is pet-down-but-on-cooldown and must not
        # count against the floor.
        summoned_at: dict[tuple[str, str], float] = {}
        for row in reader:
            if (row.get("action") or "").strip() != str(args.summon_spell):
                continue
            key = (row.get("match_id", ""), row.get("bot_guid", ""))
            try:
                t = float(row.get("time_ms", 0) or 0)
            except ValueError:
                continue
            if key not in summoned_at or t < summoned_at[key]:
                summoned_at[key] = t

    with args.csv.open(newline="", encoding="utf-8", errors="replace") as f:
        for row in csv.DictReader(f):
            if row.get("in_duel") not in ("1", "1.0"):
                continue
            match_id = (row.get("match_id") or "").strip()
            if args.holdout_only and not (
                match_id.isdigit() and int(match_id) % HOLDOUT_MOD == HOLDOUT_REMAINDER
            ):
                continue
            try:
                if float(row.get(cf, 0) or 0) < 0.5:
                    continue
                xs.append([float(row.get(f"f{i}", 0) or 0) for i in range(m["input_dim"])])
            except ValueError:
                continue
            e = (row.get("expert_action") or "").strip()
            experts.append(int(e) if e.isdigit() else -1)
            kept_rows.append(row)
            if len(xs) >= args.max_states:
                break
    if not xs:
        sys.exit("no states sampled" + (" (holdout is match_id % 10 == 7; pass --no-holdout-only "
                                        "for a CSV the head never trained on)" if args.holdout_only else ""))
    X = np.asarray(xs, dtype=np.float32)
    H = np.maximum(X @ m["w1"].T + m["b1"], 0.0)
    logits = H @ m["w2"].T + m["b2"]
    if args.exclude:
        for j, sid in enumerate(m["vocab"]):
            if sid in set(args.exclude):
                logits[:, j] = -np.inf
    top = np.argmax(logits, axis=1)
    counts = collections.Counter(int(m["vocab"][t]) for t in top)
    n = len(top)
    scope = "holdout" if args.holdout_only else "all matches"
    print(f"{args.pbml.name}: states={n} ({scope}) distinct_argmax={len(counts)}")
    for sid, c in counts.most_common(12):
        print(f"  spell {sid:6d}  {c / n * 100:5.1f}%")
    pred_ids = np.asarray([int(m["vocab"][t]) for t in top], dtype=np.int64)

    exp = np.asarray(experts, dtype=np.int64)
    mask = exp > 0
    if mask.any():
        agree = float(np.mean(pred_ids[mask] == exp[mask]))
        print(f"  expert-agreement: {agree * 100:.1f}% over {int(mask.sum())} labeled states")

    # DEC-044 starvation floor: an anti-starvation check, not a behavior mandate - it only
    # separates "summon is dead in this policy" from "summon is used selectively", so reactive
    # deferral stays learnable in the remaining states.
    floor = 0.10 if args.pet_down_floor is None else args.pet_down_floor
    if floor > 0 and args.summon_spell in m["vocab"]:
        if not has_pet_pack:
            # Pre-duel_v6 data cannot answer "was the pet down" - the column did not exist. Say so
            # rather than reporting a gate that never ran; demand the data only if asked explicitly.
            if args.pet_down_floor is not None:
                sys.exit(f"--pet-down-floor needs the duel_v6 CF_PET pack ({pet_f} missing from {args.csv})")
            print(f"  pet-down floor: SKIPPED - {args.csv.name} predates the CF_PET pack ({pet_f} absent)")
            return
        ready = []
        for i, row in enumerate(kept_rows):
            try:
                if float(row.get(pet_f, 0) or 0) >= 0.5:
                    continue
                t = float(row.get("time_ms", 0) or 0)
            except ValueError:
                continue
            cast = summoned_at.get((row.get("match_id", ""), row.get("bot_guid", "")))
            if cast is not None and t > cast:
                continue
            ready.append(i)

        if not ready:
            sys.exit("pet-down floor: no pet-down summon-ready states sampled - widen the CSV window")
        share = float(np.mean(pred_ids[ready] == args.summon_spell))
        verdict = "PASS" if share >= floor else "FAIL"
        print(f"  pet-down floor: spell {args.summon_spell} argmax in {share * 100:.1f}% of "
              f"{len(ready)} pet-down summon-ready states "
              f"(floor {floor * 100:.0f}%) -> {verdict}")
        if verdict == "FAIL":
            sys.exit(1)


if __name__ == "__main__":
    main()
