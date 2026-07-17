#!/usr/bin/env python3
"""
Train a tiny PBML1 MLP ranker from ml_decisions*.csv logs.

Usage:
  python train_ranker.py --csv ml_decisions_duel_v3.csv --out duel_warrior.pbml --duel-only --self-class warrior
  python train_ranker.py --csv ml_decisions_duel_v4.csv --out duel_mage.pbml --duel-only --self-class mage
  python train_ranker.py --csv ml_decisions.csv --out hybrid_ranker.pbml

Requires: numpy (pip install numpy)

Schemas:
  duel_v4 / DEC-025: meta + expert_action + 70 combat features + 8 action flags
  duel_v3 / DEC-017: meta (no expert_action) + 70 + 8  (header optional)
  legacy v2:         12 combat features + 8 action flags (20 floats)
  legacy v1:         12 + 6 flags (18) with --allow-legacy-18
"""

from __future__ import annotations

import argparse
import csv
import math
import re
from pathlib import Path

import numpy as np

from action_flags import fill_action_flags

ACTION_COLS_V2 = [f"a{i}" for i in range(8)]
ACTION_COLS_V1 = [f"a{i}" for i in range(6)]
HIDDEN = 64

# Class pack one-hot indices inside f0..f69 (FEATURES.md Class pack 12–21).
SELF_CLASS_F = {
    "warrior": 12,
    "paladin": 13,
    "hunter": 14,
    "rogue": 15,
    "priest": 16,
    "deathknight": 17,
    "dk": 17,
    "shaman": 18,
    "mage": 19,
    "warlock": 20,
    "druid": 21,
}

# Logger meta columns (duel_v3 headerless / written).
DUEL_V3_META = [
    "episode_id",
    "match_id",
    "bot_guid",
    "time_ms",
    "action",
    "reward",
    "short_reward",
    "terminal",
    "heuristic",
    "final_score",
    "in_duel",
]
DUEL_V4_META = [
    "episode_id",
    "match_id",
    "bot_guid",
    "time_ms",
    "action",
    "expert_action",
    "reward",
    "short_reward",
    "terminal",
    "heuristic",
    "final_score",
    "in_duel",
]

# Mirror CombatDecisionUtil::IsMetaAction (trainer-side safety net for old CSVs).
META_SUBSTR = (
    "set facing", "reach melee", "reach spell", "check mount", "check objective", "reset objective",
    "move to objective", "move to start", "move to", "xp gain", "drop target", "dps assist",
    "apply oil", "apply stone", "auto release", "self resurrect", "follow", "food", "drink",
    "unstealth", "set behind", "set pet", "toggle pet", "cast greater blessing assignment",
    "select new target", "update strategy", "chat", "emote", "rpg ", "travel", "grind", "loot",
    "add all loot", "equip", "use stone", "use oil", "wait for", "guard", "stay", "follow master",
    "ml duel bracket", "accept duel", "activate primary spec", "activate secondary spec",
)

# Extra noise for duel training (mounts / queue spam / non-combat).
DUEL_NOISE_SUBSTR = (
    "gryphon", "wind rider", "warhorse", "stallion", "steed", "talon", "drake", "wyvern",
    "mount", "lfg leave", "flee", "battle stance", "berserker stance", "defensive stance",
    "melee", "auto attack", "duel_start",
)


def is_meta_action(name: str) -> bool:
    n = name.lower()
    return any(s in n for s in META_SUBSTR)


def is_duel_noise_action(name: str) -> bool:
    n = name.lower()
    return any(s in n for s in DUEL_NOISE_SUBSTR)


def duel_fieldnames_for_width(n_cols: int) -> list[str] | None:
    """Infer duel CSV fieldnames from column count when the file has no header."""
    n_feat_flags = 70 + 8
    if n_cols == len(DUEL_V3_META) + n_feat_flags:
        return DUEL_V3_META + [f"f{i}" for i in range(70)] + ACTION_COLS_V2
    if n_cols == len(DUEL_V4_META) + n_feat_flags:
        return DUEL_V4_META + [f"f{i}" for i in range(70)] + ACTION_COLS_V2
    return None


def detect_feature_cols(fieldnames: list[str]) -> list[str]:
    idxs = []
    for name in fieldnames:
        m = re.fullmatch(r"f(\d+)", name)
        if m:
            idxs.append(int(m.group(1)))
    if not idxs:
        raise SystemExit("CSV has no f0..fN feature columns")
    n = max(idxs) + 1
    cols = [f"f{i}" for i in range(n)]
    missing = [c for c in cols if c not in fieldnames]
    if missing:
        raise SystemExit(f"CSV feature columns not contiguous; missing {missing[:5]}...")
    return cols


def resolve_fieldnames(path: Path) -> tuple[list[str], bool]:
    """Return (fieldnames, headerless). Injects duel_v3/v4 header when missing."""
    with path.open(newline="") as f:
        sample = f.readline()
    if not sample:
        raise SystemExit(f"Empty CSV: {path}")
    first = next(csv.reader([sample]))
    looks_header = bool(first) and (first[0] == "episode_id" or str(first[0]).startswith("episode"))
    if looks_header:
        return list(first), False
    inferred = duel_fieldnames_for_width(len(first))
    if not inferred:
        raise SystemExit(
            f"Headerless CSV with {len(first)} cols not recognized as duel_v3/v4. "
            "Prepend a header or pass a headed file."
        )
    print(f"schema: injected headerless duel layout cols={len(first)}")
    return inferred, True


def load_dataset(
    paths: list[Path],
    pvp_only: bool,
    pve_only: bool,
    arena_only: bool,
    duel_only: bool,
    drop_meta: bool,
    allow_legacy_18: bool,
    terminal_only: bool,
    drop_duel_noise: bool,
    self_class: str | None,
    max_rows: int,
    imitate_expert: bool,
    imitate_target: float,
):
    xs, ys = [], []
    skipped_meta = skipped_zone = skipped_bad = skipped_term = skipped_noise = skipped_class = 0
    expert_examples = 0
    class_f = SELF_CLASS_F.get(self_class.lower()) if self_class else None

    for path in paths:
        fieldnames, headerless = resolve_fieldnames(path)
        feature_cols = detect_feature_cols(fieldnames)
        has_v2 = all(c in fieldnames for c in ACTION_COLS_V2)
        has_v1 = all(c in fieldnames for c in ACTION_COLS_V1)
        if has_v2:
            action_cols = ACTION_COLS_V2
        elif has_v1 and allow_legacy_18:
            action_cols = ACTION_COLS_V1
        else:
            raise SystemExit(
                f"CSV missing a0..a7. Found fields={fieldnames[:20]}... "
                "Collect new logs, or pass --allow-legacy-18 for old 18-D files."
            )

        has_expert = "expert_action" in fieldnames
        # Aggregate OK: v3 contributes reward rows; v4 adds expert score-up when present.
        if imitate_expert and not has_expert:
            print(f"note [{path.name}]: no expert_action — reward rows only (no score-up)")
        expected = len(feature_cols) + 8
        print(
            f"schema [{path.name}]: features={len(feature_cols)} flags={len(action_cols)} "
            f"input_dim~={expected} expert_action={has_expert} headerless={headerless}"
        )

        with path.open(newline="") as f:
            reader = csv.DictReader(f, fieldnames=fieldnames if headerless else None)
            for row in reader:
                if row.get("episode_id") == "episode_id":
                    continue
                in_bg = row.get("in_bg", "0") == "1"
                in_arena = row.get("in_arena", "0") == "1"
                in_duel = row.get("in_duel", "0") == "1"
                if duel_only and not in_duel:
                    skipped_zone += 1
                    continue
                if arena_only and not in_arena:
                    skipped_zone += 1
                    continue
                if pvp_only and not in_bg and not in_arena and not in_duel:
                    skipped_zone += 1
                    continue
                if pve_only and (in_bg or in_arena or in_duel):
                    skipped_zone += 1
                    continue
                if terminal_only:
                    try:
                        if abs(float(row.get("terminal", "0"))) != 1.0:
                            skipped_term += 1
                            continue
                    except ValueError:
                        skipped_term += 1
                        continue
                action = row.get("action", "")
                if drop_meta and is_meta_action(action):
                    skipped_meta += 1
                    continue
                if drop_duel_noise and is_duel_noise_action(action):
                    skipped_noise += 1
                    continue
                try:
                    feats = [float(row[c]) for c in feature_cols]
                    if class_f is not None and feats[class_f] < 0.5:
                        skipped_class += 1
                        continue
                    flags = [float(row[c]) for c in action_cols]
                    if len(action_cols) == 6:
                        flags.extend([0.0, 0.0])
                    y = float(row["reward"])
                    # Outcome row for the action actually taken (aggregate / expert-off).
                    xs.append(feats + flags)
                    ys.append(y)
                    # DEC-025 DAgger: score-up Softmax-stock τ=0 expert on learner states.
                    if imitate_expert and has_expert:
                        expert = (row.get("expert_action") or "").strip()
                        if expert and expert != action:
                            xs.append(feats + fill_action_flags(expert))
                            ys.append(max(y, imitate_target))
                            expert_examples += 1
                except (KeyError, ValueError):
                    skipped_bad += 1
                    continue

    if not xs:
        raise SystemExit(f"No usable rows in {paths}")
    X = np.asarray(xs, dtype=np.float32)
    y = np.asarray(ys, dtype=np.float32)
    if max_rows and len(y) > max_rows:
        # Subsample after aggregate so early CSVs (e.g. v3) cannot starve v4 DAgger rows.
        rng = np.random.default_rng(0)
        idx = rng.choice(len(y), size=max_rows, replace=False)
        X = X[idx]
        y = y[idx]
        print(f"subsample: {len(ys)} -> {max_rows}")
    # Duel λ=25 dominates; keep a wider clip than old ±3 short-only logs.
    y = np.clip(y, -30.0, 30.0)
    print(
        f"filter: kept={len(y)} expert_scoreup={expert_examples} skipped_meta={skipped_meta} "
        f"skipped_zone={skipped_zone} skipped_term={skipped_term} skipped_noise={skipped_noise} "
        f"skipped_class={skipped_class} skipped_bad={skipped_bad} input_dim={X.shape[1]}"
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
    path.parent.mkdir(parents=True, exist_ok=True)
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
    ap = argparse.ArgumentParser(description="Train PBML1 hybrid/pvp/pve/duel ranker from decision logs")
    ap.add_argument("--csv", type=Path, nargs="+", required=True, help="ml_decisions*.csv path(s); aggregate all")
    ap.add_argument("--out", type=Path, required=True, help="output .pbml path")
    ap.add_argument("--pvp-only", action="store_true", help="train only on BG/arena/duel rows")
    ap.add_argument("--arena-only", action="store_true", help="train only on in_arena=1 rows")
    ap.add_argument("--duel-only", action="store_true", help="train only on in_duel=1 rows")
    ap.add_argument("--pve-only", action="store_true", help="train only on non-BG/non-arena/non-duel rows")
    ap.add_argument("--terminal-only", action="store_true", help="keep rows with terminal ∈ {+1,-1}")
    ap.add_argument("--drop-duel-noise", action="store_true",
                    help="drop stance/melee/auto-attack/mount/duel_start spam for cleaner duel training")
    ap.add_argument("--keep-meta", action="store_true", help="do not drop meta/navigation actions")
    ap.add_argument("--allow-legacy-18", action="store_true", help="accept old a0..a5 CSVs (pads to 20)")
    ap.add_argument("--self-class", type=str, default="",
                    help="keep rows where self class one-hot is set (warrior|mage|...)")
    ap.add_argument("--max-rows", type=int, default=0, help="cap kept rows after filters (0 = all)")
    ap.add_argument("--imitate-expert", action="store_true",
                    help="DAgger score-up: add expert_action flag vectors with elevated target (duel_v4)")
    ap.add_argument("--imitate-target", type=float, default=25.0,
                    help="minimum regression target for expert score-up examples")
    ap.add_argument("--epochs", type=int, default=40)
    ap.add_argument("--lr", type=float, default=1e-2)
    ap.add_argument("--hidden", type=int, default=64, help="hidden layer width")
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()
    if sum([args.pvp_only, args.pve_only, args.arena_only, args.duel_only]) > 1:
        raise SystemExit("Use only one of --pvp-only / --pve-only / --arena-only / --duel-only")
    if args.self_class and args.self_class.lower() not in SELF_CLASS_F:
        raise SystemExit(f"--self-class must be one of {sorted(SELF_CLASS_F)}")

    global HIDDEN
    HIDDEN = args.hidden

    X, y = load_dataset(
        args.csv,
        pvp_only=args.pvp_only,
        pve_only=args.pve_only,
        arena_only=args.arena_only,
        duel_only=args.duel_only,
        drop_meta=not args.keep_meta,
        allow_legacy_18=args.allow_legacy_18,
        terminal_only=args.terminal_only,
        drop_duel_noise=args.drop_duel_noise,
        self_class=args.self_class or None,
        max_rows=args.max_rows,
        imitate_expert=args.imitate_expert,
        imitate_target=args.imitate_target,
    )
    w1, b1, w2, b2 = train(X, y, epochs=args.epochs, lr=args.lr, seed=args.seed)
    write_pbml(args.out, w1, b1, w2, b2)


if __name__ == "__main__":
    main()
