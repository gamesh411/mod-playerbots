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
# Matches with match_id % 10 == 7 never reach the optimizer: they are the holdout the DEC-042
# capacity sweep and the deploy gate score on. Same split as the movement trainer, so a match held
# out of one channel is held out of both.
HOLDOUT_MOD = 10
HOLDOUT_REMAINDER = 7


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


def _probe_csv(path: Path) -> tuple[bool, int, list[str] | None]:
    """Sniff revision and feature width from the first line only.

    Returns (has_header, feature_dim, meta), where meta is the positional column list for
    the headerless revisions and None once the file carries its own header. feature_dim 0
    means the file is empty and the caller should skip it.
    """
    with path.open(newline="", encoding="utf-8", errors="replace") as f:
        sample = f.read(4096)
    if not sample:
        return True, 0, None
    first = next(csv.reader([sample.splitlines()[0]]))
    if "episode_id" in first:
        return True, _header_feature_dim(first), None
    if len(first) >= len(DUEL_V5_META) + DUEL_V5_FEATURES + 8:  # duel_v5 = 113 cols
        return False, DUEL_V5_FEATURES, DUEL_V5_META
    has_expert = len(first) >= 12 + LEGACY_FEATURE_DIM
    return False, LEGACY_FEATURE_DIM, (DUEL_V4_META if has_expert else DUEL_V3_META)


def _iter_rows(path: Path, *, has_header: bool, meta: list[str] | None, feature_dim: int):
    """Stream one CSV as row dicts, reopening the file on each call.

    Deliberately a generator rather than a list: outcomes and features need two passes, and
    holding a farm log as dicts costs roughly 6 KB per row - 16 GB for a 1 GB duel_v6 log,
    enough to OOM the box out from under the running server. Two streaming passes trade
    re-reading the file for constant memory, and the file is only ever read from disk cache.
    """
    with path.open(newline="", encoding="utf-8", errors="replace") as f:
        if has_header:
            yield from csv.DictReader(f)
            return
        feat_start = len(meta)
        for cols in csv.reader(f):
            if len(cols) < feat_start + feature_dim:
                continue
            row = {meta[i]: cols[i] for i in range(len(meta))}
            for i in range(feature_dim):
                row[f"f{i}"] = cols[feat_start + i]
            yield row


def load_spellbook_rows(
    paths: list[Path],
    *,
    self_class: str | None,
    drop_meta: bool,
    drop_duel_noise: bool,
    max_rows: int,
):
    """Return (X[n,d], action_ids[n], expert_ids[n] or -1, won[n], match_ids[n]).

    d is the CSV's feature width. Holdout rows are returned too - the caller splits on
    match_ids, so one pass over a multi-hundred-MB farm serves both training and scoring.
    """
    xs: list[list[float]] = []
    actions: list[int] = []
    experts: list[int] = []
    wons: list[bool] = []
    match_ids: list[int] = []
    class_idx = SELF_CLASS_F.get((self_class or "").lower())
    feature_dim: int | None = None

    for path in paths:
        has_header, path_dim, meta = _probe_csv(path)
        if not path_dim:
            continue

        # Mixing widths would silently train on a ragged matrix; DEC-043 forbids zero-fill,
        # so a mismatch is a fatal input error, not something to pad around.
        if feature_dim is None:
            feature_dim = path_dim
        elif path_dim != feature_dim:
            raise SystemExit(
                f"{path}: feature width {path_dim} != {feature_dim} from earlier inputs; "
                "do not mix CSV revisions in one training set"
            )

        stream = dict(has_header=has_header, meta=meta, feature_dim=path_dim)

        # Per-bot episode outcome: last terminal!=0 row per (match_id, bot_guid),
        # same convention as eval_duel_winrate.py.
        outcome: dict[tuple[str, str], float] = {}
        for row in _iter_rows(path, **stream):
            try:
                term = float(row.get("terminal", "0") or 0)
            except ValueError:
                continue
            if term != 0.0:
                outcome[(row.get("match_id", ""), row.get("bot_guid", ""))] = term

        for row in _iter_rows(path, **stream):
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

            raw_match = (row.get("match_id") or "").strip()
            xs.append(feats)
            actions.append(int(action))
            experts.append(expert_id)
            match_ids.append(int(raw_match) if raw_match.isdigit() else -1)
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
        np.asarray(match_ids, dtype=np.int64),
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


def report_holdout(X, y, expert_ids, vocab: list[int], w1, b1, w2, b2, *, hidden: int):
    """DEC-042 capacity-sweep scoreboard: CE and teacher agreement on never-trained matches."""
    if not len(y):
        print("holdout: none (no match_id % 10 == 7 rows survived label mapping)")
        return
    h = np.maximum(X @ w1.T + b1, 0.0)
    logits = h @ w2.T + b2
    logits -= np.max(logits, axis=1, keepdims=True)
    probs = np.exp(logits)
    probs /= np.sum(probs, axis=1, keepdims=True)
    ce = float(np.mean(-np.log(np.clip(probs[np.arange(len(y)), y], 1e-8, 1.0))))
    top = np.argmax(logits, axis=1)
    acc = float(np.mean(top == y))
    pred_ids = np.asarray([vocab[t] for t in top], dtype=np.int64)
    labeled = expert_ids > 0
    agree = float(np.mean(pred_ids[labeled] == expert_ids[labeled])) if labeled.any() else float("nan")
    distinct = len(set(int(s) for s in pred_ids))
    print(f"holdout hidden={hidden} rows={len(y)} ce={ce:.5f} argmax_acc={acc:.4f} "
          f"teacher_agreement={agree:.4f} distinct_argmax={distinct}/{len(vocab)}")


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
                    help="train CE on expert_action, dropping rows that carry no teacher label")
    ap.add_argument("--drop-labels", type=int, nargs="*", default=[],
                    help="spell ids never used as CE labels (rows resolving to them are dropped); "
                         "e.g. 6603 once auto-attack became engine scaffolding (DEC-030)")
    ap.add_argument("--balance-labels", choices=["none", "sqrt", "inv"], default="none",
                    help="weight CE rows by inverse label frequency (sqrt = 1/sqrt(freq)); "
                         "counters marginal-mode argmax collapse onto always-legal actions")
    ap.add_argument("--label-scheme", choices=["action", "expert", "win-else-expert", "win-only"], default=None,
                    help="CE target: 'action' = logged action (behavior cloning); "
                         "'expert' = expert_action, dropping rows that have none (same as "
                         "--imitate-expert); "
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

    X, action_ids, expert_ids, won, match_ids = load_spellbook_rows(
        args.csv,
        self_class=args.self_class,
        drop_meta=not args.keep_meta,
        drop_duel_noise=args.drop_duel_noise,
        max_rows=args.max_rows,
    )
    held = (match_ids % HOLDOUT_MOD) == HOLDOUT_REMAINDER

    if args.vocab_file:
        vocab = [int(line.strip()) for line in args.vocab_file.read_text().splitlines() if line.strip().isdigit()]
    else:
        # Training rows only: a vocab widened by holdout actions would let the split leak into
        # the head's output layer.
        vocab = build_vocab(action_ids[~held], expert_ids[~held])
    if not vocab:
        raise SystemExit("Empty vocab")
    index = {sid: i for i, sid in enumerate(vocab)}

    drop_labels = set(args.drop_labels)
    targets = []
    keep = []
    n_win_self = 0
    for i, aid in enumerate(action_ids.tolist()):
        if scheme == "expert":
            # An unlabelled row is a tick the teacher had no castable pick for, not a tick it
            # agreed with the action. Falling back to `aid` trains the head on its own explore
            # noise (DEC-049) - on the M2 round-0 warrior seat that would have been most rows.
            if expert_ids[i] <= 0:
                continue
            label = int(expert_ids[i])
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
    keep = np.asarray(keep)
    y_all = np.asarray(targets, dtype=np.int64)
    kept_held = held[keep]
    X_hold = X[keep[kept_held]]
    y_hold = y_all[kept_held]
    expert_hold = expert_ids[keep[kept_held]]
    X = X[keep[~kept_held]]
    y = y_all[~kept_held]
    if not len(y):
        raise SystemExit("No training rows outside the holdout; farm more matches")
    print(f"rows={len(y)} holdout={len(y_hold)} vocab={len(vocab)} scheme={scheme} "
          f"win_self_rows={n_win_self} won_frac={float(won.mean()):.3f} "
          f"balance={args.balance_labels}")

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
    report_holdout(X_hold, y_hold, expert_hold, vocab, w1, b1, w2, b2, hidden=args.hidden)
    write_pbml_multi(args.out, w1, b1, w2, b2, vocab)


if __name__ == "__main__":
    main()
