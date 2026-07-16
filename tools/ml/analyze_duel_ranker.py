#!/usr/bin/env python3
"""Train-time analysis: probe duel_ranker.pbml + summarize duel_v2 strategies."""

from __future__ import annotations

import argparse
import collections
import csv
from pathlib import Path

import numpy as np

NOISE = (
    "gryphon", "wind rider", "warhorse", "stallion", "steed", "mount", "lfg leave", "flee",
    "battle stance", "berserker stance", "defensive stance", "melee", "auto attack",
)

CANDIDATES = {
    "warrior": [
        "pummel", "shield bash", "heroic strike", "cleave", "mortal strike", "execute", "slam",
        "overpower", "charge", "intercept", "hamstring", "intimidating shout", "bladestorm",
        "recklessness", "shield wall", "last stand", "berserker rage", "bloodrage", "rend",
        "whirlwind", "sweeping strikes", "disarm", "spell reflection", "retaliation",
        "victory rush", "thunder clap", "piercing howl",
    ],
    "mage": [
        "counterspell", "frostbolt", "ice lance", "fireball", "frost nova", "blink", "ice block",
        "icy veins", "deep freeze", "polymorph", "mana shield", "evocation", "arcane explosion",
        "flamestrike", "fire blast", "cone of cold", "blizzard", "slow", "spellsteal",
        "mirror image", "presence of mind", "cold snap", "mage armor", "frost armor",
        "molten armor", "arcane intellect", "dampen magic", "shoot",
    ],
}


def load_pbml(path: Path):
    txt = path.read_text().split()
    d: dict = {}
    i = 0
    assert txt[i] == "PBML1"
    i += 1
    while i < len(txt):
        k = txt[i]
        i += 1
        if k in ("input_dim", "hidden_dim", "output_dim"):
            d[k] = int(txt[i])
            i += 1
        elif k == "W1":
            n = d["hidden_dim"] * d["input_dim"]
            d["W1"] = np.array([float(x) for x in txt[i : i + n]], dtype=np.float32).reshape(
                d["hidden_dim"], d["input_dim"]
            )
            i += n
        elif k == "b1":
            n = d["hidden_dim"]
            d["b1"] = np.array([float(x) for x in txt[i : i + n]], dtype=np.float32)
            i += n
        elif k == "W2":
            n = d["hidden_dim"]
            d["W2"] = np.array([float(x) for x in txt[i : i + n]], dtype=np.float32)
            i += n
        elif k == "b2":
            d["b2"] = float(txt[i])
            i += 1
        else:
            raise SystemExit(f"bad key {k}")
    return d


def forward(m, x: np.ndarray) -> float:
    h = np.maximum(m["W1"] @ x + m["b1"], 0)
    return float(h @ m["W2"] + m["b2"])


def flags(name: str) -> list[float]:
    n = name.lower()

    def has(*needles: str) -> bool:
        return any(s in n for s in needles)

    a = [0.0] * 8
    a[0] = 1.0 if has("pummel", "counterspell", "kick", "shield bash", "mind freeze") else 0.0
    a[1] = 1.0 if has("enemy healer") else 0.0
    a[2] = (
        1.0
        if has("ice block", "shield wall", "last stand", "divine shield", "mana shield", "cloak", "dispersion")
        else 0.0
    )
    a[3] = (
        1.0
        if has("polymorph", "frost nova", "hamstring", "intimidating", "deep freeze", "blind", "fear")
        else 0.0
    )
    a[4] = 1.0 if has("heal", "bandage") else 0.0
    a[5] = (
        1.0
        if a[0] or a[2] or a[3] or has("blink", "trinket", "berserker rage", "bloodrage", "ice lance", "fire blast")
        else 0.0
    )
    a[6] = (
        1.0
        if has(
            "frostbolt",
            "fireball",
            "heroic strike",
            "cleave",
            "mortal",
            "execute",
            "slam",
            "whirlwind",
            "arcane explosion",
            "flamestrike",
            "ice lance",
            "shoot",
            "bloodthirst",
            "rend",
            "overpower",
            "cone of cold",
            "blizzard",
            "scorch",
        )
        else 0.0
    )
    a[7] = 1.0 if has("attack enemy player", "attack duel") else 0.0
    if sum(a) == 0 and name not in ("melee", "auto attack"):
        a[6] = 1.0
    return a


def base_state(self_cls: int, foe_cls: int, self_spec: int = 0, foe_spec: int = 2) -> np.ndarray:
    x = np.zeros(70, dtype=np.float32)
    x[0] = 1.0
    x[1] = 1.0
    x[2] = 1.0
    x[3] = 1.0
    x[6] = 1.0
    x[12 + self_cls] = 1.0
    x[22 + self_spec] = 1.0
    x[25 + foe_cls] = 1.0
    x[35 + foe_spec] = 1.0
    for i in range(38, 49):
        x[i] = 1.0
    for i in range(49, 59):
        x[i] = 1.0
    x[61] = 0.5
    x[63] = 1.0
    x[64] = 1.0
    x[65] = 1.0
    x[67] = 1.0
    x[69] = 1.0
    return x


def rank(m, state: np.ndarray, actions: list[str], top: int = 8):
    scored = []
    for a in actions:
        inp = np.concatenate([state, np.array(flags(a), dtype=np.float32)])
        scored.append((forward(m, inp), a, flags(a)))
    scored.sort(reverse=True)
    return scored[:top]


def role_str(fl: list[float]) -> str:
    names = ["kick", "healFocus", "def", "cc", "heal", "instant", "dmg", "focus"]
    roles = [names[i] for i, v in enumerate(fl) if v]
    return ",".join(roles) if roles else "-"


def is_noise(action: str) -> bool:
    a = action.lower()
    return any(s in a for s in NOISE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", type=Path, default=Path(r"C:\AzerothCore-server\ml_decisions_duel_v2.csv"))
    ap.add_argument("--model", type=Path, default=Path(r"C:\AzerothCore-server\duel_ranker.pbml"))
    args = ap.parse_args()

    m = load_pbml(args.model)
    print(f"MODEL in={m['input_dim']} hidden={m['hidden_dim']} b2={m['b2']:.3f}")

    scenarios = []
    scenarios.append(("Warr vs Mage: open (mid range, both full)", base_state(0, 7), "warrior"))
    s = base_state(0, 7)
    s[4] = 1.0
    s[61] = 0.2
    s[62] = 1.0
    scenarios.append(("Warr vs Mage: foe casting (kick window)", s, "warrior"))
    s = base_state(0, 7)
    s[2] = 0.2
    s[61] = 0.05
    s[62] = 1.0
    scenarios.append(("Warr vs Mage: melee + foe low HP", s, "warrior"))
    s = base_state(0, 7)
    s[0] = 0.2
    s[60] = 1.0
    s[44] = 0.0
    scenarios.append(("Warr vs Mage: self low HP, foe IB up", s, "warrior"))
    scenarios.append(("Mage vs Warr: open (mid range)", base_state(7, 0, 2, 0), "mage"))
    s = base_state(7, 0, 2, 0)
    s[61] = 0.1
    s[62] = 1.0
    scenarios.append(("Mage vs Warr: warrior in melee", s, "mage"))
    s = base_state(7, 0, 2, 0)
    s[1] = 0.15
    scenarios.append(("Mage vs Warr: self low mana", s, "mage"))
    s = base_state(7, 0, 2, 0)
    s[61] = 0.2
    s[46] = 1.0
    scenarios.append(("Mage vs Warr: foe gap close ready", s, "mage"))

    print("\n========== MODEL ACTION RANKINGS (role-flag net) ==========")
    for title, state, cls in scenarios:
        print(f"\n## {title}")
        for score, a, fl in rank(m, state, CANDIDATES[cls], 8):
            print(f"  {score:+7.2f}  {a:22s}  [{role_str(fl)}]")

    print("\n========== ROLE PREFERENCE (flag ablation, Warr open) ==========")
    st = base_state(0, 7)
    print(f"  zero-flags baseline: {forward(m, np.concatenate([st, np.zeros(8, np.float32)])):.2f}")
    role_names = ["interrupt", "enemy_healer", "defensive", "cc", "heal", "instant", "damage", "focus_player"]
    for i, name in enumerate(role_names):
        fl = np.zeros(8, np.float32)
        fl[i] = 1.0
        print(f"  {name:14s}  {forward(m, np.concatenate([st, fl])):+.2f}")
    for combo, bits in [
        ("kick+dmg", [0, 6]),
        ("cc+dmg", [3, 6]),
        ("def only", [2]),
        ("instant+dmg", [5, 6]),
        ("kick+instant", [0, 5]),
    ]:
        fl = np.zeros(8, np.float32)
        for b in bits:
            fl[b] = 1.0
        print(f"  combo {combo:14s}  {forward(m, np.concatenate([st, fl])):+.2f}")

    print("\n========== EMPIRICAL DATA (terminal, no stance/melee/mounts) ==========")
    rows = []
    with args.csv.open(newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r.get("episode_id") == "episode_id":
                continue
            if abs(float(r["terminal"])) != 1:
                continue
            if is_noise(r["action"]):
                continue
            rows.append(r)

    def is_warr(r):
        return float(r.get("f12", 0)) == 1

    def is_mage(r):
        return float(r.get("f19", 0)) == 1

    for label, pred in [("WARRIOR", is_warr), ("MAGE", is_mage)]:
        subset = [r for r in rows if pred(r)]
        wins = [r for r in subset if float(r["terminal"]) > 0]
        print(
            f"\n## {label} n={len(subset)} win_rows={len(wins)} "
            f"row_winrate={len(wins) / max(1, len(subset)):.1%}"
        )
        aw = collections.defaultdict(lambda: [0, 0])
        for r in subset:
            aw[r["action"]][0] += 1
            if float(r["terminal"]) > 0:
                aw[r["action"]][1] += 1
        ranked = sorted(((n, w / n, a) for a, (n, w) in aw.items() if n >= 25), key=lambda x: (-x[1], -x[0]))
        print("  high win-assoc:")
        for n, wr, a in ranked[:12]:
            print(f"    {wr:5.1%} n={n:4d}  {a}")
        print("  low win-assoc:")
        for n, wr, a in ranked[-8:]:
            print(f"    {wr:5.1%} n={n:4d}  {a}")

    print("\n========== SITUATIONAL SLICES ==========")
    cast = [r for r in rows if float(r.get("f4", 0)) >= 0.5]
    kickish = sum(1 for r in cast if any(k in r["action"].lower() for k in ("pummel", "counterspell", "shield bash")))
    print(f"foe_casting rows={len(cast)} interrupt_share={kickish / max(1, len(cast)):.1%}")
    print("  top:", collections.Counter(r["action"] for r in cast).most_common(8))

    melee_w = [r for r in rows if is_warr(r) and float(r.get("f62", 0)) >= 0.5]
    print(f"warrior_in_melee rows={len(melee_w)}")
    print("  top:", collections.Counter(r["action"] for r in melee_w).most_common(8))

    imm = [r for r in rows if float(r.get("f60", 0)) >= 0.5]
    print(f"foe_immunity rows={len(imm)}")
    print("  top:", collections.Counter(r["action"] for r in imm).most_common(8))

    print("\n========== MODEL vs DATA (sample) ==========")
    rng = np.random.default_rng(0)
    sample = rows if len(rows) < 8000 else list(rng.choice(rows, 8000, replace=False))
    by_act: dict[str, list[tuple[float, float]]] = collections.defaultdict(list)
    for r in sample:
        feats = np.array([float(r[f"f{i}"]) for i in range(70)], dtype=np.float32)
        fl = np.array(flags(r["action"]), dtype=np.float32)
        pred = forward(m, np.concatenate([feats, fl]))
        by_act[r["action"]].append((pred, float(r["reward"])))
    agg = []
    for a, vals in by_act.items():
        if len(vals) < 20:
            continue
        preds = [v[0] for v in vals]
        rews = [v[1] for v in vals]
        agg.append((float(np.mean(preds)), float(np.mean(rews)), len(vals), a))
    agg.sort(reverse=True)
    print("top by model score:")
    for mp, mr, n, a in agg[:15]:
        print(f"  model={mp:+6.2f} dataR={mr:+6.2f} n={n:4d}  {a}")
    print("bottom by model score:")
    for mp, mr, n, a in agg[-10:]:
        print(f"  model={mp:+6.2f} dataR={mr:+6.2f} n={n:4d}  {a}")


if __name__ == "__main__":
    main()
