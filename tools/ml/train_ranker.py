#!/usr/bin/env python3
"""
Train a tiny PBML1 MLP ranker from ml_decisions.csv logs.

Usage:
  python3 train_ranker.py --csv ml_decisions.csv --out hybrid_ranker.pbml
  python3 train_ranker.py --csv ml_decisions.csv --out pvp_ranker.pbml --pvp-only
  python3 train_ranker.py --csv ml_decisions.csv --out hybrid_ranker.pbml --pve-only

Requires: numpy (pip install numpy)

v2 schema: 12 combat features + 8 action flags (20 floats).
Older CSVs with a0..a5 only are rejected unless --allow-legacy-18.
"""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path

import numpy as np

FEATURE_COLS = [f"f{i}" for i in range(12)]
ACTION_COLS_V2 = [f"a{i}" for i in range(8)]
ACTION_COLS_V1 = [f"a{i}" for i in range(6)]
INPUT_DIM_V2 = 20
INPUT_DIM_V1 = 18
HIDDEN = 32

# Mirror CombatDecisionUtil::IsMetaAction (trainer-side safety net for old CSVs).
META_SUBSTR = (
    "set facing", "reach melee", "reach spell", "check mount", "check objective", "reset objective",
    "move to objective", "move to start", "move to", "xp gain", "drop target", "dps assist",
    "apply oil", "apply stone", "auto release", "self resurrect", "follow", "food", "drink",
    "unstealth", "set behind", "set pet", "toggle pet", "cast greater blessing assignment",
    "select new target", "update strategy", "chat", "emote", "rpg ", "travel", "grind", "loot",
    "add all loot", "equip", "use stone", "use oil", "wait for", "guard", "stay", "follow master",
)


def is_meta_action(name: str) -> bool:
    n = name.lower()
    return any(s in n for s in META_SUBSTR)


def load_dataset(path: Path, pvp_only: bool, pve_only: bool, drop_meta: bool, allow_legacy_18: bool):
    xs, ys = [], []
    skipped_meta = skipped_zone = skipped_bad = 0
    with path.open(newline="") as f:
        reader = csv.DictReader(f)
        fieldnames = reader.fieldnames or []
        has_v2 = all(c in fieldnames for c in ACTION_COLS_V2)
        has_v1 = all(c in fieldnames for c in ACTION_COLS_V1)
        if has_v2:
            action_cols = ACTION_COLS_V2
            input_dim = INPUT_DIM_V2
        elif has_v1 and allow_legacy_18:
            action_cols = ACTION_COLS_V1
            input_dim = INPUT_DIM_V1
        else:
            raise SystemExit(
                f"CSV missing a0..a7 (v2). Found fields={fieldnames[:20]}... "
                "Collect new logs after the v2 logger, or pass --allow-legacy-18 for old 18-D files."
            )

        for row in reader:
            in_bg = row.get("in_bg", "0") == "1"
            in_arena = row.get("in_arena", "0") == "1"
            if pvp_only and not in_bg and not in_arena:
                skipped_zone += 1
                continue
            if pve_only and (in_bg or in_arena):
                skipped_zone += 1
                continue
            action = row.get("action", "")
            if drop_meta and is_meta_action(action):
                skipped_meta += 1
                continue
            try:
                x = [float(row[c]) for c in FEATURE_COLS] + [float(row[c]) for c in action_cols]
                if input_dim == INPUT_DIM_V1:
                    x.extend([0.0, 0.0])  # pad damage/focus flags
                y = float(row["reward"])
            except (KeyError, ValueError):
                skipped_bad += 1
                continue
            xs.append(x)
            ys.append(y)
    if not xs:
        raise SystemExit(f"No usable rows in {path}")
    X = np.asarray(xs, dtype=np.float32)
    y = np.asarray(ys, dtype=np.float32)
    y = np.clip(y, -3.0, 3.0)
    print(
        f"filter: kept={len(y)} skipped_meta={skipped_meta} skipped_zone={skipped_zone} skipped_bad={skipped_bad}"
    )
    return X, y


def init_mlp(rng: np.random.Generator, input_dim: int):
    w1 = rng.normal(0, math.sqrt(2 / input_dim), size=(HIDDEN, input_dim)).astype(np.float32)
    b1 = np.zeros(HIDDEN, dtype=np.float32)
    w2 = rng.normal(0, math.sqrt(2 / HIDDEN), size=(HIDDEN,)).astype(np.float32)
    b2 = np.float32(0.0)
    return w1, b1, w2, b2


def train(X, y, epochs=40, lr=1e-2, batch=64, seed=0):
    rng = np.random.default_rng(seed)
    input_dim = X.shape[1]
    w1, b1, w2, b2 = init_mlp(rng, input_dim)
    n = X.shape[0]
    idx = np.arange(n)
    for ep in range(epochs):
        rng.shuffle(idx)
        total = 0.0
        for start in range(0, n, batch):
            batch_idx = idx[start : start + batch]
            xb = X[batch_idx]
            yb = y[batch_idx]
            h_pre = xb @ w1.T + b1
            h = np.maximum(h_pre, 0.0)
            pred = h @ w2 + b2
            err = pred - yb
            total += float(np.mean(err * err))
            dpred = (2.0 / len(batch_idx)) * err
            dw2 = h.T @ dpred
            db2 = np.sum(dpred)
            dh = dpred[:, None] * w2[None, :]
            dh[h_pre <= 0.0] = 0.0
            dw1 = dh.T @ xb
            db1 = np.sum(dh, axis=0)
            w2 -= lr * dw2.astype(np.float32)
            b2 -= lr * np.float32(db2)
            w1 -= lr * dw1.astype(np.float32)
            b1 -= lr * db1.astype(np.float32)
        if (ep + 1) % 10 == 0 or ep == 0:
            print(f"epoch {ep+1}/{epochs} mse={total / max(1, n // batch):.5f}")
    return w1, b1, w2, b2


def write_pbml(path: Path, w1, b1, w2, b2):
    input_dim = w1.shape[1]
    with path.open("w") as f:
        f.write("PBML1\n")
        f.write(f"input_dim {input_dim}\n")
        f.write(f"hidden_dim {HIDDEN}\n")
        f.write("output_dim 1\n")
        f.write("W1\n")
        f.write(" ".join(f"{v:.8f}" for v in w1.reshape(-1)) + "\n")
        f.write("b1\n")
        f.write(" ".join(f"{v:.8f}" for v in b1) + "\n")
        f.write("W2\n")
        f.write(" ".join(f"{v:.8f}" for v in w2) + "\n")
        f.write(f"b2\n{float(b2):.8f}\n")
    print(f"Wrote {path} (input_dim={input_dim})")


def main():
    ap = argparse.ArgumentParser(description="Train PBML1 hybrid/pvp/pve ranker from decision logs")
    ap.add_argument("--csv", type=Path, required=True, help="ml_decisions.csv path")
    ap.add_argument("--out", type=Path, required=True, help="output .pbml path")
    ap.add_argument("--pvp-only", action="store_true", help="train only on BG/arena rows")
    ap.add_argument("--pve-only", action="store_true", help="train only on non-BG/non-arena rows")
    ap.add_argument("--keep-meta", action="store_true", help="do not drop meta/navigation actions")
    ap.add_argument("--allow-legacy-18", action="store_true", help="accept old a0..a5 CSVs (pads to 20)")
    ap.add_argument("--epochs", type=int, default=40)
    ap.add_argument("--lr", type=float, default=1e-2)
    args = ap.parse_args()
    if args.pvp_only and args.pve_only:
        raise SystemExit("Use only one of --pvp-only / --pve-only")

    X, y = load_dataset(
        args.csv,
        pvp_only=args.pvp_only,
        pve_only=args.pve_only,
        drop_meta=not args.keep_meta,
        allow_legacy_18=args.allow_legacy_18,
    )
    print(f"Loaded {len(y)} samples, reward mean={y.mean():.3f} std={y.std():.3f} dim={X.shape[1]}")
    w1, b1, w2, b2 = train(X, y, epochs=args.epochs, lr=args.lr)
    write_pbml(args.out, w1, b1, w2, b2)


if __name__ == "__main__":
    main()
