#!/usr/bin/env python3
"""
DEC-039 offline degeneracy gate for M1 movement heads + movement-quality metrics.

Runs the PBML argmax over held-out real states (match_id % 10 == 7, never trained on) and
checks, before every deploy:
  1. teacher agreement (bootstrap gate: >= 0.90 held-out agreement with expert_movement_intent),
  2. the mover band structure per distance band
     (Frost: retreat < 15, hold 15-30 with LoS, approach > 30 or no-LoS;
      Arms: hold in melee, toward out of melee),
  3. state-conditional argmax - no unconditional single-intent mode (> 95% one intent = FAIL).

Later heads (DAgger / expert-off) drop gates 1-2 via --no-agreement-gate / --no-band-gate;
gate 3 always applies.

Also prints per-seat movement-quality metrics (stage-card numbers): melee uptime,
cast-band uptime, mean foe distance, snare-sprint proxy.

Usage:
  python check_movement_argmax.py --pbml ../../artifacts/duel/m1/warrior.pbml \
    --csv ml_movement_duel_v1.csv --self-class warrior
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np

from train_movement_ranker import CLASS_IDS, MOVE_FEATURES, N_INTENTS, load_movement_rows

INTENT_NAMES = [
    "hold", "toward", "toward_left", "left", "away_left", "away", "away_right", "right", "toward_right",
]
TOWARDISH = {1, 2, 8}
AWAYISH = {4, 5, 6}

# Feature indices (CombatDecisionFeatures.h).
F_DIST_NORM = 61   # distance / 40, clamped [0,1]
F_IN_MELEE = 62
F_IN_LOS = 63
F_FOE_SNARE = 80   # CF_MOVE_FOE_SNARE_FRAC
SELF_SPEED = 70    # CF_MOVE_SELF_SPEED_FRAC


def load_pbml(path: Path):
    tokens = path.read_text().split()
    it = iter(tokens)
    if next(it) != "PBML1":
        raise SystemExit(f"{path}: bad magic")
    dims = {}
    w = {}
    vocab = []
    key = next(it, None)
    while key is not None:
        if key in ("input_dim", "hidden_dim", "output_dim"):
            dims[key] = int(next(it))
            key = next(it, None)
        elif key == "vocab":
            n = int(next(it))
            vocab = [int(next(it)) for _ in range(n)]
            key = next(it, None)
        elif key in ("W1", "b1", "W2", "b2"):
            sizes = {
                "W1": dims["hidden_dim"] * dims["input_dim"],
                "b1": dims["hidden_dim"],
                "W2": dims.get("output_dim", 1) * dims["hidden_dim"],
                "b2": dims.get("output_dim", 1),
            }
            w[key] = np.asarray([float(next(it)) for _ in range(sizes[key])], dtype=np.float32)
            key = next(it, None)
        else:
            raise SystemExit(f"{path}: unknown key {key}")
    w1 = w["W1"].reshape(dims["hidden_dim"], dims["input_dim"])
    w2 = w["W2"].reshape(dims.get("output_dim", 1), dims["hidden_dim"])
    return w1, w["b1"], w2, w["b2"], vocab


def argmax_forward(X, w1, b1, w2, b2):
    h = np.maximum(X @ w1.T + b1, 0.0)
    return np.argmax(h @ w2.T + b2, axis=1)


F_FOE_ROOTED = 81  # CF_MOVE_FOE_ROOTED


def band_of(row, is_mage: bool) -> str:
    if not is_mage:
        return "melee" if row[F_IN_MELEE] > 0.5 else "out_of_melee"
    d = row[F_DIST_NORM] * 40.0
    if row[F_IN_LOS] < 0.5 or d > 30.0:
        return "approach_band"
    if d < 15.0:
        return "retreat_band"
    # DEC-036: inside 15-30 the mover holds ONLY while the foe can chase; an impaired foe
    # opens the snare-window sprint (bank distance), so those states expect AWAY.
    if row[F_FOE_SNARE] > 0.05 or row[F_FOE_ROOTED] > 0.5:
        return "stand_band_sprint"
    return "stand_band"


# Expected dominant argmax intent sets per band (DEC-036 mover structure).
EXPECTED = {
    "melee": {0},
    "out_of_melee": TOWARDISH,
    "retreat_band": AWAYISH,
    "stand_band": {0},
    "stand_band_sprint": AWAYISH,
    "approach_band": TOWARDISH,
}


def main():
    ap = argparse.ArgumentParser(description="M1 movement-head degeneracy gate (DEC-039)")
    ap.add_argument("--pbml", type=Path, required=True)
    ap.add_argument("--csv", type=Path, nargs="+", required=True)
    ap.add_argument("--self-class", type=str, required=True, choices=sorted(CLASS_IDS))
    ap.add_argument("--min-agreement", type=float, default=0.90)
    ap.add_argument("--sample", type=int, default=20000, help="minimum states to sample (uses all holdout rows)")
    ap.add_argument("--no-agreement-gate", action="store_true",
                    help="later heads: skip the >= 90%% teacher-agreement gate")
    ap.add_argument("--no-band-gate", action="store_true",
                    help="later heads: skip the band-structure gate (state-conditionality still gates)")
    ap.add_argument("--min-band-rows", type=int, default=200,
                    help="bands with fewer holdout rows are reported but not gated")
    args = ap.parse_args()

    w1, b1, w2, b2, vocab = load_pbml(args.pbml)
    if w1.shape[1] != MOVE_FEATURES or w2.shape[0] != N_INTENTS or vocab != list(range(N_INTENTS)):
        raise SystemExit(f"{args.pbml}: not a 90-D / 9-logit movement head (in={w1.shape[1]}, out={w2.shape[0]})")

    class_id = CLASS_IDS[args.self_class]
    is_mage = args.self_class == "mage"

    # Holdout matches only: match_id % 10 == 7 (never seen by the trainer).
    X_all, intents, experts, _, _, match_ids = load_movement_rows(
        args.csv, class_id=class_id, include_holdout=True)
    hold_mask = match_ids % 10 == 7
    if not hold_mask.any():
        raise SystemExit("No holdout rows (match_id % 10 == 7); farm more data")

    X = X_all[hold_mask]
    expert = experts[hold_mask]
    print(f"holdout states: {len(X)} (target >= {args.sample})")
    if len(X) < args.sample:
        print(f"WARNING: fewer than {args.sample} holdout states; gate is advisory at this volume")

    pred = argmax_forward(X, w1, b1, w2, b2)

    failures = []

    agreement = float((pred == expert).mean())
    print(f"\nteacher agreement (holdout argmax): {agreement:.4f}")
    if not args.no_agreement_gate and agreement < args.min_agreement:
        failures.append(f"agreement {agreement:.4f} < {args.min_agreement}")

    share = np.bincount(pred, minlength=N_INTENTS) / len(pred)
    print("argmax intent share:", {INTENT_NAMES[i]: round(float(s), 4) for i, s in enumerate(share) if s > 0})
    if float(share.max()) > 0.95:
        failures.append(f"unconditional mode: {INTENT_NAMES[int(share.argmax())]} at {share.max():.3f} > 0.95")

    print("\nband structure (argmax share per band, expected dominant in caps):")
    bands = np.asarray([band_of(row, is_mage) for row in X])
    for band in sorted(set(bands)):
        sel = bands == band
        n_band = int(sel.sum())
        dist = np.bincount(pred[sel], minlength=N_INTENTS) / max(1, n_band)
        expected = EXPECTED[band]
        exp_share = float(sum(dist[i] for i in expected))
        top = int(np.argmax(dist))
        gated = n_band >= args.min_band_rows
        print(f"  {band:17s} n={n_band:7d} expected_share={exp_share:.3f} top={INTENT_NAMES[top]}"
              + ("" if gated else "  [advisory: small band]"))
        if not args.no_band_gate and gated and (top not in expected):
            failures.append(f"band {band}: top intent {INTENT_NAMES[top]} not in expected set")

    # Movement-quality metrics over ALL rows of these CSVs (stage-card numbers, report-only).
    print("\nmovement-quality metrics (all rows, this seat):")
    d_all = X_all[:, F_DIST_NORM] * 40.0
    if is_mage:
        cast_band = (X_all[:, F_IN_LOS] > 0.5) & (d_all >= 15.0) & (d_all <= 30.0)
        print(f"  cast-band uptime (15-30y, LoS): {float(cast_band.mean()):.4f}")
    else:
        print(f"  melee uptime: {float((X_all[:, F_IN_MELEE] > 0.5).mean()):.4f}")
    print(f"  mean foe distance: {float(d_all.mean()):.2f} y")
    sprint_proxy = (X_all[:, F_FOE_SNARE] > 0.3) & (np.isin(intents, list(AWAYISH))) & (X_all[:, SELF_SPEED] > 0.9)
    print(f"  snare-sprint proxy rows: {int(sprint_proxy.sum())} ({float(sprint_proxy.mean()):.4f})")
    print(f"  teacher-disagreement rate (executed vs expert, all rows): "
          f"{float((intents != experts).mean()):.4f}")

    if failures:
        print("\nDEGENERACY GATE: FAIL")
        for f_ in failures:
            print(f"  - {f_}")
        raise SystemExit(1)
    print("\nDEGENERACY GATE: PASS")


if __name__ == "__main__":
    main()
