#!/usr/bin/env python3
"""
Stock↔stock / seat winrate eval from duel decision CSVs (DEC-025 freeze gate helper).

Uses terminal labels on class-filtered rows. A match contributes once per (match_id, bot class)
using the last terminal≠0 row for that bot in the match.

Usage:
  python eval_duel_winrate.py --csv ml_decisions_duel_v3.csv --label stock-stock
  python eval_duel_winrate.py --csv ml_decisions_duel_v4.csv --label ranker-farm --baseline-csv ml_decisions_duel_v3.csv
"""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path

from train_ranker import SELF_CLASS_F, resolve_fieldnames

CLASS_NAMES = {
    12: "warrior",
    19: "mage",
}


def iter_rows(path: Path):
    fieldnames, headerless = resolve_fieldnames(path)
    with path.open(newline="") as f:
        reader = csv.DictReader(f, fieldnames=fieldnames if headerless else None)
        for row in reader:
            if row.get("episode_id") == "episode_id":
                continue
            yield row


def self_class_idx(row: dict) -> int | None:
    for fidx, name in CLASS_NAMES.items():
        try:
            if float(row.get(f"f{fidx}", "0")) >= 0.5:
                return fidx
        except ValueError:
            continue
    # Fallback scan f12..f21
    for i in range(12, 22):
        try:
            if float(row.get(f"f{i}", "0")) >= 0.5:
                return i
        except ValueError:
            continue
    return None


def match_outcomes(path: Path) -> dict[str, dict[str, int]]:
    """class_name -> {wins, losses, matches} from terminal labels."""
    # match_id -> class_idx -> last terminal
    last: dict[str, dict[int, float]] = defaultdict(dict)
    for row in iter_rows(path):
        if row.get("in_duel", "0") != "1":
            continue
        try:
            term = float(row.get("terminal", "0"))
        except ValueError:
            continue
        if abs(term) != 1.0:
            continue
        mid = row.get("match_id", "")
        if not mid or mid == "0":
            continue
        cls = self_class_idx(row)
        if cls is None:
            continue
        last[mid][cls] = term

    stats = {name: {"wins": 0, "losses": 0, "matches": 0} for name in CLASS_NAMES.values()}
    for _mid, by_cls in last.items():
        for cls, term in by_cls.items():
            name = CLASS_NAMES.get(cls)
            if not name:
                continue
            stats[name]["matches"] += 1
            if term > 0:
                stats[name]["wins"] += 1
            else:
                stats[name]["losses"] += 1
    return stats


def winrate(s: dict[str, int]) -> float | None:
    n = s["wins"] + s["losses"]
    if n == 0:
        return None
    return s["wins"] / n


def print_stats(label: str, stats: dict[str, dict[str, int]]):
    print(f"\n=== {label} ===")
    for name in ("warrior", "mage"):
        s = stats[name]
        wr = winrate(s)
        wr_s = f"{wr*100:.1f}%" if wr is not None else "n/a"
        print(f"  {name:8s}  WR={wr_s:7s}  W={s['wins']}  L={s['losses']}  matches={s['matches']}")


def main():
    ap = argparse.ArgumentParser(description="Duel seat winrate eval from CSV terminals")
    ap.add_argument("--csv", type=Path, required=True)
    ap.add_argument("--label", type=str, default="eval")
    ap.add_argument("--baseline-csv", type=Path, default=None, help="stock↔stock reference CSV")
    ap.add_argument("--delta", type=float, default=0.02, help="WR uplift gate δ (fraction)")
    args = ap.parse_args()

    stats = match_outcomes(args.csv)
    print_stats(args.label, stats)

    if args.baseline_csv:
        base = match_outcomes(args.baseline_csv)
        print_stats("baseline (stock-stock)", base)
        print("\n=== uplift vs baseline (same-policy seats; freeze needs mixed seats) ===")
        both_clear = True
        for name in ("warrior", "mage"):
            wr = winrate(stats[name])
            bwr = winrate(base[name])
            if wr is None or bwr is None:
                print(f"  {name}: insufficient data")
                both_clear = False
                continue
            uplift = wr - bwr
            ok = uplift >= args.delta
            both_clear = both_clear and ok
            print(
                f"  {name}: {wr*100:.1f}% vs {bwr*100:.1f}%  d={uplift*100:+.1f}pp  "
                f"{'PASS' if ok else 'FAIL'} (need >={args.delta*100:.0f}pp)"
            )
        print(f"\nSame-policy gate both seats: {'PASS' if both_clear else 'FAIL'}")
        print("Note: DEC-025 freeze is mixed seats (ranker vs stock), not ranker-ranker.")


if __name__ == "__main__":
    main()
