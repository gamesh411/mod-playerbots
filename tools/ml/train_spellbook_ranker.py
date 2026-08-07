#!/usr/bin/env python3
"""
Train S2 multi-logit PBML1 spellbook ranker (DEC-026).

State features (70-D) -> logits over frozen spell-id vocab.
CE on chosen action (and expert_action when --imitate-expert).
No weight warm-start from S1.

Usage:
  python train_spellbook_ranker.py --csv ml_decisions_duel_v4.csv \\
    --out ../../artifacts/duel/s2/warrior.pbml --self-class warrior --duel-only
  python train_spellbook_ranker.py --csv v4.csv --out mage.pbml --self-class mage \\
    --duel-only --imitate-expert
"""

from __future__ import annotations

import argparse
import csv
import math
import re
from pathlib import Path

import numpy as np

from train_ranker import (
    DUEL_V3_META,
    DUEL_V4_META,
    DUEL_V5_META,
    DUEL_V5_FEATURES,
    SELF_CLASS_F,
    is_duel_noise_action,
    is_meta_action,
)

# Feature width of the pre-movement CSVs (duel_v3/v4). Headed files carry their own width in
# f0..fN instead: duel_v5 is 90, duel_v6 (DEC-043, CF_PET + CF_FORM) is 112. The head's input_dim
# follows the data, so a duel_v6 farm trains a 112-D M2 head with no zero-fill (DEC-042/043).
LEGACY_FEATURE_DIM = 70
HIDDEN = 64


def _is_spell_id(s: str) -> bool:
    return bool(s) and s.isdigit()


def _header_feature_dim(fieldnames: list[str]) -> int:
    n = 0
    for name in fieldnames or []:
        m = re.fullmatch(r"f(\d+)", name)
        if m:
            n = max(n, int(m.group(1)) + 1)
    if not n:
        raise SystemExit("CSV has no f0..fN feature columns")
    return n


def load_spellbook_rows(
    paths: list[Path],
    *,
    self_class: str | None,
    drop_meta: bool,
    drop_duel_noise: bool,
    max_rows: int,
):
    """Return (X[n,d], action_ids[n], expert_ids[n] or -1, won[n]); d is the CSV's feature width."""
    xs: list[list[float]] = []
    actions: list[int] = []
    experts: list[int] = []
    wons: list[bool] = []
    class_idx = SELF_CLASS_F.get((self_class or "").lower())
    feature_dim: int | None = None

    for path in paths:
        with path.open(newline="", encoding="utf-8", errors="replace") as f:
            sample = f.read(4096)
            f.seek(0)
            has_header = "episode_id" in sample.splitlines()[0] if sample else False
            if has_header:
                reader = csv.DictReader(f)
                rows = list(reader)
                has_expert = "expert_action" in (reader.fieldnames or [])
                path_dim = _header_feature_dim(reader.fieldnames or [])
            else:
                # Headerless: detect v3 vs v4 vs v5 by column count.
                raw = list(csv.reader(f))
                if not raw:
                    continue
                width = len(raw[0])
                path_dim = LEGACY_FEATURE_DIM
                if width >= len(DUEL_V5_META) + DUEL_V5_FEATURES + 8:  # duel_v5 = 113 cols
                    has_expert = True
                    meta = DUEL_V5_META
                    path_dim = DUEL_V5_FEATURES
                else:
                    has_expert = width >= 12 + LEGACY_FEATURE_DIM
                    meta = DUEL_V4_META if has_expert else DUEL_V3_META
                feat_start = len(meta)
                rows = []
                for cols in raw:
                    if len(cols) < feat_start + path_dim:
                        continue
                    d = {meta[i]: cols[i] for i in range(len(meta))}
                    for i in range(path_dim):
                        d[f"f{i}"] = cols[feat_start + i]
                    rows.append(d)

            # Mixing widths would silently train on a ragged matrix; DEC-043 forbids zero-fill,
            # so a mismatch is a fatal input error, not something to pad around.
            if feature_dim is None:
                feature_dim = path_dim
            elif path_dim != feature_dim:
                raise SystemExit(
                    f"{path}: feature width {path_dim} != {feature_dim} from earlier inputs; "
                    "do not mix CSV revisions in one training set"
                )

            # Per-bot episode outcome: last terminal!=0 row per (match_id, bot_guid),
            # same convention as eval_duel_winrate.py.
            outcome: dict[tuple[str, str], float] = {}
            for row in rows:
                try:
                    term = float(row.get("terminal", "0") or 0)
                except ValueError:
                    continue
                if term != 0.0:
                    outcome[(row.get("match_id", ""), row.get("bot_guid", ""))] = term

            for row in rows:
                if row.get("in_duel", "1") not in ("1", "1.0", "true", "True"):
                    continue
                action = (row.get("action") or "").strip()
                if not _is_spell_id(action):
                    continue
                if drop_meta and is_meta_action(action):
                    continue
                if drop_duel_noise and is_duel_noise_action(action):
                    continue
                if class_idx is not None:
                    try:
                        if float(row.get(f"f{class_idx}", 0) or 0) < 0.5:
                            continue
                    except ValueError:
                        continue

                feats = []
                ok = True
                for i in range(feature_dim):
                    try:
                        feats.append(float(row.get(f"f{i}", 0) or 0))
                    except ValueError:
                        ok = False
                        break
                if not ok:
                    continue

                expert = (row.get("expert_action") or "").strip()
                expert_id = int(expert) if _is_spell_id(expert) else -1

                xs.append(feats)
                actions.append(int(action))
                experts.append(expert_id)
                wons.append(outcome.get((row.get("match_id", ""), row.get("bot_guid", "")), 0.0) > 0.0)
                if max_rows and len(xs) >= max_rows:
                    break
            if max_rows and len(xs) >= max_rows:
                break

    if not xs:
        raise SystemExit("No spell-id duel rows kept (need ActionPolicy=ranker SpellPool=spellbook logs)")
    return (
        np.asarray(xs, dtype=np.float32),
        np.asarray(actions, dtype=np.int64),
        np.asarray(experts, dtype=np.int64),
        np.asarray(wons, dtype=bool),
    )


def build_vocab(action_ids: np.ndarray, expert_ids: np.ndarray) -> list[int]:
    ids = set(int(x) for x in action_ids.tolist())
    for x in expert_ids.tolist():
        if int(x) > 0:
            ids.add(int(x))
    return sorted(ids)


def train_ce(
    X: np.ndarray,
    y_idx: np.ndarray,
    n_out: int,
    *,
    epochs: int,
    lr: float,
    seed: int,
    hidden: int,
    sample_w: np.ndarray | None = None,
):
    rng = np.random.default_rng(seed)
    n, d = X.shape
    w1 = (rng.normal(0, 0.05, size=(hidden, d))).astype(np.float32)
    b1 = np.zeros(hidden, dtype=np.float32)
    w2 = (rng.normal(0, 0.05, size=(n_out, hidden))).astype(np.float32)
    b2 = np.zeros(n_out, dtype=np.float32)

    idx = np.arange(n)
    batch = 256
    for ep in range(epochs):
        rng.shuffle(idx)
        total_loss = 0.0
        n_batches = 0
        for start in range(0, n, batch):
            bi = idx[start : start + batch]
            xb = X[bi]
            yb = y_idx[bi]
            wb = sample_w[bi] if sample_w is not None else None
            h_pre = xb @ w1.T + b1
            h = np.maximum(h_pre, 0.0)
            logits = h @ w2.T + b2
            # Stable softmax
            m = np.max(logits, axis=1, keepdims=True)
            exp = np.exp(logits - m)
            probs = exp / np.sum(exp, axis=1, keepdims=True)
            # CE loss
            rows = np.arange(len(bi))
            nll = -np.log(np.clip(probs[rows, yb], 1e-8, 1.0))
            if wb is not None:
                loss = float(np.sum(nll * wb) / np.sum(wb))
            else:
                loss = float(np.mean(nll))
            total_loss += loss
            n_batches += 1

            dlogits = probs
            dlogits[rows, yb] -= 1.0
            if wb is not None:
                dlogits *= (wb / np.mean(wb))[:, None]
            dlogits /= len(bi)

            dw2 = dlogits.T @ h
            db2 = np.sum(dlogits, axis=0)
            dh = dlogits @ w2
            dh[h_pre <= 0.0] = 0.0
            dw1 = dh.T @ xb
            db1 = np.sum(dh, axis=0)

            w2 -= lr * dw2.astype(np.float32)
            b2 -= lr * db2.astype(np.float32)
            w1 -= lr * dw1.astype(np.float32)
            b1 -= lr * db1.astype(np.float32)

        if (ep + 1) % 10 == 0 or ep == 0:
            print(f"epoch {ep+1}/{epochs} ce={total_loss / max(1, n_batches):.5f}")
    return w1, b1, w2, b2


def write_pbml_multi(path: Path, w1, b1, w2, b2, vocab: list[int]):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as f:
        f.write("PBML1\n")
        f.write(f"input_dim {w1.shape[1]}\n")
        f.write(f"hidden_dim {w1.shape[0]}\n")
        f.write(f"output_dim {len(vocab)}\n")
        f.write(f"vocab {len(vocab)}\n")
        f.write(" ".join(str(v) for v in vocab) + "\n")
        f.write("W1\n")
        f.write(" ".join(f"{v:.8f}" for v in w1.reshape(-1)) + "\n")
        f.write("b1\n")
        f.write(" ".join(f"{v:.8f}" for v in b1) + "\n")
        f.write("W2\n")
        f.write(" ".join(f"{v:.8f}" for v in w2.reshape(-1)) + "\n")
        f.write("b2\n")
        f.write(" ".join(f"{v:.8f}" for v in b2) + "\n")
    print(f"Wrote {path} (in={w1.shape[1]}, out={len(vocab)}, vocab={len(vocab)})")


def main():
    ap = argparse.ArgumentParser(description="Train S2 multi-logit spellbook PBML (DEC-026)")
    ap.add_argument("--csv", type=Path, nargs="+", required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--self-class", type=str, required=True)
    ap.add_argument("--duel-only", action="store_true", default=True)
    ap.add_argument("--drop-duel-noise", action="store_true")
    ap.add_argument("--keep-meta", action="store_true")
    ap.add_argument("--max-rows", type=int, default=0)
    ap.add_argument("--imitate-expert", action="store_true",
                    help="train CE on expert_action when present, else on logged action")
    ap.add_argument("--drop-labels", type=int, nargs="*", default=[],
                    help="spell ids never used as CE labels (rows resolving to them are dropped); "
                         "e.g. 6603 once auto-attack became engine scaffolding (DEC-030)")
    ap.add_argument("--balance-labels", choices=["none", "sqrt", "inv"], default="none",
                    help="weight CE rows by inverse label frequency (sqrt = 1/sqrt(freq)); "
                         "counters marginal-mode argmax collapse onto always-legal actions")
    ap.add_argument("--label-scheme", choices=["action", "expert", "win-else-expert", "win-only"], default=None,
                    help="CE target: 'action' = logged action (behavior cloning); "
                         "'expert' = expert_action fallback logged action (same as --imitate-expert); "
                         "'win-else-expert' = logged action on WON episodes, else expert_action, "
                         "else drop the row (DEC-029 win-anchored self-imitation); "
                         "'win-only' = logged action on WON episodes, drop everything else "
                         "(no expert fallback - for seats whose teacher projection is degenerate)")
    ap.add_argument("--epochs", type=int, default=40)
    ap.add_argument("--lr", type=float, default=1e-2)
    ap.add_argument("--hidden", type=int, default=64)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--vocab-file", type=Path, default=None,
                    help="optional frozen spell-id list (one id per line); else build from CSV")
    args = ap.parse_args()
    if args.self_class.lower() not in SELF_CLASS_F:
        raise SystemExit(f"--self-class must be one of {sorted(SELF_CLASS_F)}")

    scheme = args.label_scheme or ("expert" if args.imitate_expert else "action")

    X, action_ids, expert_ids, won = load_spellbook_rows(
        args.csv,
        self_class=args.self_class,
        drop_meta=not args.keep_meta,
        drop_duel_noise=args.drop_duel_noise,
        max_rows=args.max_rows,
    )

    if args.vocab_file:
        vocab = [int(line.strip()) for line in args.vocab_file.read_text().splitlines() if line.strip().isdigit()]
    else:
        vocab = build_vocab(action_ids, expert_ids)
    if not vocab:
        raise SystemExit("Empty vocab")
    index = {sid: i for i, sid in enumerate(vocab)}

    drop_labels = set(args.drop_labels)
    targets = []
    keep = []
    n_win_self = 0
    for i, aid in enumerate(action_ids.tolist()):
        if scheme == "expert":
            label = expert_ids[i] if expert_ids[i] > 0 else aid
        elif scheme == "win-else-expert":
            if won[i] and aid in index and aid not in drop_labels:
                label = aid
                n_win_self += 1
            elif expert_ids[i] > 0:
                label = int(expert_ids[i])
            else:
                continue
        elif scheme == "win-only":
            if not won[i] or aid not in index or aid in drop_labels:
                continue
            label = aid
            n_win_self += 1
        else:
            label = aid
        if label in drop_labels or label not in index:
            continue
        targets.append(index[label])
        keep.append(i)
    if not keep:
        raise SystemExit("No rows mapped into vocab")
    X = X[np.asarray(keep)]
    y = np.asarray(targets, dtype=np.int64)
    print(f"rows={len(y)} vocab={len(vocab)} scheme={scheme} win_self_rows={n_win_self} "
          f"won_frac={float(won.mean()):.3f} balance={args.balance_labels}")

    sample_w = None
    if args.balance_labels != "none":
        counts = np.bincount(y, minlength=len(vocab)).astype(np.float64)
        freq = counts[y] / len(y)
        sample_w = (1.0 / np.sqrt(freq) if args.balance_labels == "sqrt" else 1.0 / freq)
        sample_w = (sample_w / sample_w.mean()).astype(np.float32)

    w1, b1, w2, b2 = train_ce(
        X, y, len(vocab), epochs=args.epochs, lr=args.lr, seed=args.seed, hidden=args.hidden,
        sample_w=sample_w,
    )
    write_pbml_multi(args.out, w1, b1, w2, b2, vocab)


if __name__ == "__main__":
    main()
