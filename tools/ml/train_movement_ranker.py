#!/usr/bin/env python3
"""
Train the M1 movement-intent ranker (DEC-039): 90-D state -> 9 intent logits.

Trains on the dedicated movement CSV (ml_movement_duel_v1.csv) written by the executor
subtick at the 500 ms intent horizon - never on ability rows.
Matches with match_id % 10 == 7 are held out for check_movement_argmax.py and are
never trained on.

Usage:
  # Round B bootstrap (behavior-clone the M0 scripted teacher):
  python train_movement_ranker.py --csv ml_movement_duel_v1.csv \
    --out ../../artifacts/duel/m1/warrior.pbml --self-class warrior --label-scheme expert \
    --balance-labels sqrt --max-rows 400000

  # DAgger rounds: same, on the aggregate of all round CSVs.
  # Expert-off: --label-scheme win-else-expert.

Output: PBML1 multi-logit with vocab 0..8 (MlMovementIntent order), input_dim 90.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np

from train_spellbook_ranker import train_ce, write_pbml_multi

MOVE_FEATURES = 90
N_INTENTS = 9
HOLDOUT_MOD = 10
HOLDOUT_REMAINDER = 7
CLASS_IDS = {"warrior": 1, "mage": 8}

META_COLS = [
    "match_id",
    "bot_guid",
    "bot_class",
    "time_ms",
    "movement_intent",
    "expert_movement_intent",
    "realized_heading",
    "reward",
    "terminal",
]


def load_movement_rows(paths: list[Path], *, class_id: int, include_holdout: bool = False):
    """Return (X[n,90], intents[n], experts[n], won[n], ep_return[n], match_ids[n]) for one class."""
    xs: list[np.ndarray] = []
    intents: list[int] = []
    experts: list[int] = []
    keys: list[tuple[str, str]] = []
    match_ids: list[int] = []

    outcome: dict[tuple[str, str], float] = {}
    ep_return: dict[tuple[str, str], float] = {}

    n_holdout = 0
    for file_i, path in enumerate(paths):
        with path.open(newline="", encoding="utf-8", errors="replace") as f:
            header = f.readline().strip().split(",")
            idx = {name: i for i, name in enumerate(header)}
            missing = [c for c in META_COLS if c not in idx]
            if missing or f"f{MOVE_FEATURES - 1}" not in idx:
                raise SystemExit(f"{path}: not an ml_movement_duel_v1 file (missing {missing[:3]}...)")
            f_start = idx["f0"]

            i_match = idx["match_id"]
            i_guid = idx["bot_guid"]
            i_class = idx["bot_class"]
            i_intent = idx["movement_intent"]
            i_expert = idx["expert_movement_intent"]
            i_reward = idx["reward"]
            i_term = idx["terminal"]

            for line in f:
                cols = line.rstrip("\n").split(",")
                if len(cols) < f_start + MOVE_FEATURES:
                    continue
                try:
                    if int(cols[i_class]) != class_id:
                        continue
                    match_id = int(cols[i_match])
                    if not include_holdout and match_id % HOLDOUT_MOD == HOLDOUT_REMAINDER:
                        n_holdout += 1
                        continue
                    intent = int(cols[i_intent])
                    expert = int(cols[i_expert])
                    if not (0 <= intent < N_INTENTS and 0 <= expert < N_INTENTS):
                        continue
                    # File index in the key: match ids restart per server boot, so different
                    # farm windows (separate CSVs) can reuse the same id.
                    key = (file_i, cols[i_match], cols[i_guid])
                    term = float(cols[i_term])
                    reward = float(cols[i_reward])
                    feats = np.asarray(cols[f_start : f_start + MOVE_FEATURES], dtype=np.float32)
                except ValueError:
                    continue
                xs.append(feats)
                intents.append(intent)
                experts.append(expert)
                keys.append(key)
                match_ids.append(match_id)
                if term != 0.0:
                    outcome[key] = term
                ep_return[key] = ep_return.get(key, 0.0) + reward

    if not xs:
        raise SystemExit(f"No movement rows for class_id={class_id} in {[str(p) for p in paths]}")
    won = np.asarray([outcome.get(k, 0.0) > 0.0 for k in keys], dtype=bool)
    ret = np.asarray([ep_return.get(k, 0.0) for k in keys], dtype=np.float32)
    print(f"loaded rows={len(xs)} won_frac={float(won.mean()):.3f} holdout_skipped={n_holdout}")
    return (
        np.stack(xs),
        np.asarray(intents, dtype=np.int64),
        np.asarray(experts, dtype=np.int64),
        won,
        ret,
        np.asarray(match_ids, dtype=np.int64),
    )


def main():
    ap = argparse.ArgumentParser(description="Train M1 movement-intent PBML (DEC-039)")
    ap.add_argument("--csv", type=Path, nargs="+", required=True,
                    help="ml_movement_duel_v1 CSV path(s); aggregate all (DEC-025 never-drop rule)")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--self-class", type=str, required=True, choices=sorted(CLASS_IDS))
    ap.add_argument("--label-scheme", choices=["expert", "action", "win-else-expert"], default="expert",
                    help="CE target: 'expert' = teacher intent (BC / DAgger); 'action' = executed intent; "
                         "'win-else-expert' = executed intent on WON episodes else teacher (DEC-029 scheme)")
    ap.add_argument("--balance-labels", choices=["none", "sqrt", "inv"], default="sqrt",
                    help="inverse-label-frequency CE weights; sqrt is load-bearing (DEC-029/039)")
    ap.add_argument("--reward-weight", action="store_true",
                    help="escalation lever only: also weight rows by "
                         "clip(1 + 0.5*tanh(episode shaped return), 0.5, 1.5)")
    ap.add_argument("--max-rows", type=int, default=400000)
    ap.add_argument("--epochs", type=int, default=40)
    ap.add_argument("--lr", type=float, default=1e-2)
    ap.add_argument("--hidden", type=int, default=64)
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()

    X, intents, experts, won, ret, _ = load_movement_rows(args.csv, class_id=CLASS_IDS[args.self_class])

    if args.label_scheme == "expert":
        y = experts
        keep = np.ones(len(y), dtype=bool)
    elif args.label_scheme == "action":
        y = intents
        keep = np.ones(len(y), dtype=bool)
    else:  # win-else-expert
        y = np.where(won, intents, experts)
        keep = np.ones(len(y), dtype=bool)
        print(f"win-else-expert: self-labeled rows={int(won.sum())} teacher rows={int((~won).sum())}")

    X, y, ret = X[keep], y[keep], ret[keep]

    if args.max_rows and len(y) > args.max_rows:
        rng = np.random.default_rng(0)
        pick = rng.choice(len(y), size=args.max_rows, replace=False)
        X, y, ret = X[pick], y[pick], ret[pick]
        print(f"subsample: -> {args.max_rows}")

    counts = np.bincount(y, minlength=N_INTENTS)
    print("label counts:", {i: int(c) for i, c in enumerate(counts) if c})

    sample_w = None
    if args.balance_labels != "none":
        freq = counts[y].astype(np.float64) / len(y)
        sample_w = 1.0 / np.sqrt(freq) if args.balance_labels == "sqrt" else 1.0 / freq
        sample_w = (sample_w / sample_w.mean()).astype(np.float32)
    if args.reward_weight:
        rw = np.clip(1.0 + 0.5 * np.tanh(ret), 0.5, 1.5).astype(np.float32)
        sample_w = rw if sample_w is None else (sample_w * rw).astype(np.float32)
        sample_w = (sample_w / sample_w.mean()).astype(np.float32)

    w1, b1, w2, b2 = train_ce(
        X, y, N_INTENTS, epochs=args.epochs, lr=args.lr, seed=args.seed, hidden=args.hidden,
        sample_w=sample_w,
    )
    # Vocab 0..8 makes the PBML self-describing multi-logit (MlMlpModel requires a vocab).
    write_pbml_multi(args.out, w1, b1, w2, b2, list(range(N_INTENTS)))


if __name__ == "__main__":
    main()
