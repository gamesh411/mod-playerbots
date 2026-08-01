#!/usr/bin/env python3
"""Offline degeneracy check for S2 multi-logit PBML: argmax distribution over sampled states."""

import argparse
import collections
import csv
import sys
from pathlib import Path

import numpy as np

CLASS_F = {"warrior": 12, "mage": 19}


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
    ap.add_argument("--exclude", type=int, nargs="*", default=[],
                    help="spell ids masked at runtime by MlDuelSpellPool (toggles, stances); "
                         "their logits are set to -inf before argmax to mirror live behavior")
    args = ap.parse_args()

    m = load_pbml_multi(args.pbml)
    cf = f"f{CLASS_F[args.self_class]}"
    xs = []
    experts = []
    with args.csv.open(newline="", encoding="utf-8", errors="replace") as f:
        for row in csv.DictReader(f):
            if row.get("in_duel") not in ("1", "1.0"):
                continue
            try:
                if float(row.get(cf, 0) or 0) < 0.5:
                    continue
                xs.append([float(row.get(f"f{i}", 0) or 0) for i in range(m["input_dim"])])
            except ValueError:
                continue
            e = (row.get("expert_action") or "").strip()
            experts.append(int(e) if e.isdigit() else -1)
            if len(xs) >= args.max_states:
                break
    if not xs:
        sys.exit("no states sampled")
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
    print(f"{args.pbml.name}: states={n} distinct_argmax={len(counts)}")
    for sid, c in counts.most_common(12):
        print(f"  spell {sid:6d}  {c / n * 100:5.1f}%")
    exp = np.asarray(experts, dtype=np.int64)
    mask = exp > 0
    if mask.any():
        pred_ids = np.asarray([int(m["vocab"][t]) for t in top], dtype=np.int64)
        agree = float(np.mean(pred_ids[mask] == exp[mask]))
        print(f"  expert-agreement: {agree * 100:.1f}% over {int(mask.sum())} labeled states")


if __name__ == "__main__":
    main()
