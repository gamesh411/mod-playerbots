#!/usr/bin/env python3
"""
Train a tiny PBML1 MLP ranker from ml_decisions.csv logs.

Usage:
  python3 train_ranker.py --csv ml_decisions.csv --out hybrid_ranker.pbml
  python3 train_ranker.py --csv ml_decisions.csv --out pvp_ranker.pbml --pvp-only

Requires: numpy (pip install numpy)
Optional: better results with more rows; works with pure numpy SGD.
"""

from __future__ import annotations

import argparse
import csv
import math
import random
from pathlib import Path

import numpy as np

FEATURE_COLS = [f"f{i}" for i in range(12)]
ACTION_COLS = [f"a{i}" for i in range(6)]
INPUT_DIM = 18
HIDDEN = 32


def load_dataset(path: Path, pvp_only: bool):
    xs, ys = [], []
    with path.open(newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            if pvp_only and row.get("in_bg", "0") != "1" and row.get("in_arena", "0") != "1":
                continue
            try:
                x = [float(row[c]) for c in FEATURE_COLS] + [float(row[c]) for c in ACTION_COLS]
                y = float(row["reward"])
            except (KeyError, ValueError):
                continue
            xs.append(x)
            ys.append(y)
    if not xs:
        raise SystemExit(f"No usable rows in {path}")
    X = np.asarray(xs, dtype=np.float32)
    y = np.asarray(ys, dtype=np.float32)
    # Normalize targets to roughly [-2, 2] for stable training
    y = np.clip(y, -3.0, 3.0)
    return X, y


def init_mlp(rng: np.random.Generator):
    # He init
    w1 = rng.normal(0, math.sqrt(2 / INPUT_DIM), size=(HIDDEN, INPUT_DIM)).astype(np.float32)
    b1 = np.zeros(HIDDEN, dtype=np.float32)
    w2 = rng.normal(0, math.sqrt(2 / HIDDEN), size=(HIDDEN,)).astype(np.float32)
    b2 = np.float32(0.0)
    return w1, b1, w2, b2


def forward(x, w1, b1, w2, b2):
    h = x @ w1.T + b1
    h = np.maximum(h, 0.0)
    return float(h @ w2 + b2), h


def train(X, y, epochs=40, lr=1e-2, batch=64, seed=0):
    rng = np.random.default_rng(seed)
    w1, b1, w2, b2 = init_mlp(rng)
    n = X.shape[0]
    idx = np.arange(n)
    for ep in range(epochs):
        rng.shuffle(idx)
        total = 0.0
        for start in range(0, n, batch):
            batch_idx = idx[start : start + batch]
            xb = X[batch_idx]
            yb = y[batch_idx]
            # Forward
            h_pre = xb @ w1.T + b1
            h = np.maximum(h_pre, 0.0)
            pred = h @ w2 + b2
            err = pred - yb
            total += float(np.mean(err * err))
            # Backward MSE
            dpred = (2.0 / len(batch_idx)) * err
            dw2 = h.T @ dpred
            db2 = np.sum(dpred)
            dh = np.outer(dpred, w2) if dpred.ndim == 1 else dpred[:, None] * w2[None, :]
            # fix shapes: dpred is (B,), w2 is (H,)
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
    with path.open("w") as f:
        f.write("PBML1\n")
        f.write(f"input_dim {INPUT_DIM}\n")
        f.write(f"hidden_dim {HIDDEN}\n")
        f.write("output_dim 1\n")
        f.write("W1\n")
        f.write(" ".join(f"{v:.8f}" for v in w1.reshape(-1)) + "\n")
        f.write("b1\n")
        f.write(" ".join(f"{v:.8f}" for v in b1) + "\n")
        f.write("W2\n")
        f.write(" ".join(f"{v:.8f}" for v in w2) + "\n")
        f.write(f"b2\n{float(b2):.8f}\n")
    print(f"Wrote {path}")


def main():
    ap = argparse.ArgumentParser(description="Train PBML1 hybrid/pvp ranker from decision logs")
    ap.add_argument("--csv", type=Path, required=True, help="ml_decisions.csv path")
    ap.add_argument("--out", type=Path, required=True, help="output .pbml path")
    ap.add_argument("--pvp-only", action="store_true", help="train only on BG/arena rows")
    ap.add_argument("--epochs", type=int, default=40)
    ap.add_argument("--lr", type=float, default=1e-2)
    args = ap.parse_args()

    X, y = load_dataset(args.csv, args.pvp_only)
    print(f"Loaded {len(y)} samples, reward mean={y.mean():.3f} std={y.std():.3f}")
    w1, b1, w2, b2 = train(X, y, epochs=args.epochs, lr=args.lr)
    write_pbml(args.out, w1, b1, w2, b2)


if __name__ == "__main__":
    main()
