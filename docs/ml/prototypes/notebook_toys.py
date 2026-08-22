# PROTOTYPE - throwaway (ticket #39, wayfinder map #35).
# Rough versions of the six seminar notebook toys, for reaction only.
# The real notebook gets rebuilt from scratch in the seminar repo (#45).
#
# Run: .\run_notebook_toys.ps1  (creates a venv, installs pins, opens marimo)
#
# /// script
# requires-python = ">=3.12"
# dependencies = ["marimo", "numpy", "pandas", "altair"]
# ///

import marimo

__generated_with = "0.24.0"
app = marimo.App(width="medium")


@app.cell
def _():
    import marimo as mo
    import numpy as np
    import pandas as pd
    import altair as alt

    # dataviz reference palette (validated): fixed categorical order + status colors
    pal = {
        "blue": "#2a78d6",     # warrior, everywhere
        "orange": "#eb6834",   # mage, everywhere
        "aqua": "#1baf7a",
        "yellow": "#eda100",
        "magenta": "#e87ba4",
        "green": "#008300",
        "good": "#0ca30c",
        "crit": "#d03b3b",
        "gray": "#898781",
    }
    return alt, mo, np, pd, pal


@app.cell
def _(mo):
    mo.md(
        """
        # Learning to duel: six toys

        **PROTOTYPE** - rough cuts of the six seminar notebook sections, built to react to, not to keep.

        Each section pairs a **one-interaction toy** (one slider, nothing else) with the
        **real frozen numbers** from the duel RL curriculum (Arms warrior vs Frost mage,
        WotLK playerbots).
        Sections 1, 2, and 6 are the three live audience moments in the talk;
        the rest are for phone browsing.

        **React to:** which toys teach, which confuse, what to cut or sharpen.
        """
    )
    return


@app.cell
def _(mo):
    mo.md(
        """
        ## 1 - What is a policy? Softmax and temperature (S0) - *live moment 1*

        A policy maps **state in, action out**, every 100 ms.
        The baseline stage S0 keeps the bot's stock scripted scores and adds one knob:
        **temperature**.
        Low temperature = always take the top-scored action (exploit).
        High temperature = spread the picks (explore) - which is what generates varied
        training data.

        **Real numbers:** the farm runs at **tau = 10** (explore, millions of logged
        decisions); demos and freeze evals run at **tau = 0** (argmax).
        The relevance shape below is illustrative - the real scores come from the stock
        Engine at runtime.
        """
    )
    return


@app.cell
def _(mo):
    tau = mo.ui.slider(start=0.0, stop=10.0, step=0.5, value=10.0, label="temperature tau")
    tau
    return (tau,)


@app.cell
def _(alt, mo, np, pd, pal, tau):
    _actions = [
        "Mortal Strike", "Overpower", "Execute", "Rend",
        "Hamstring", "Heroic Strike", "Battle Shout", "Auto-attack",
    ]
    _rel = np.array([4.0, 3.1, 2.6, 2.2, 1.7, 1.3, 0.9, 0.4])
    _t = tau.value
    if _t < 0.25:
        _p = np.zeros_like(_rel)
        _p[int(np.argmax(_rel))] = 1.0
    else:
        _z = _rel / _t
        _e = np.exp(_z - _z.max())
        _p = _e / _e.sum()
    _df = pd.DataFrame({"action": _actions, "prob": _p})
    _chart = (
        alt.Chart(_df)
        .mark_bar(color=pal["blue"], cornerRadiusEnd=4, height=14)
        .encode(
            x=alt.X("prob:Q", scale=alt.Scale(domain=[0, 1]), title="P(action picked)"),
            y=alt.Y("action:N", sort=None, title=None),
        )
        .properties(width=520, height=230, title=f"Softmax over stock relevance, tau = {_t:g}")
    )
    _H = float(-(_p * np.log(np.clip(_p, 1e-12, 1.0))).sum())
    _msg = mo.md(
        f"At **tau = {_t:g}** the bot effectively chooses among **{np.exp(_H):.1f}** of "
        f"{len(_actions)} actions. "
        + ("This is the demo/freeze setting: pure argmax, fully repeatable." if _t < 0.25
           else "This is near the farm setting: varied duels, varied logs - the raw material for learning." if _t >= 8
           else "Between the two: some exploration, mostly the script.")
    )
    mo.vstack([_chart, _msg])
    return


@app.cell
def _(mo):
    mo.md(
        """
        ## 2 - How do you know it's better? Winrate confidence (S1) - *live moment 2*

        S1 trained a ranker and had to answer: **did it actually beat stock?**
        The gate: beat the stock baseline by **+2pp** (delta = 0.02).
        A winrate from N duels is an estimate with error bars - drag N and watch when the
        verdict becomes readable at all.

        **Real numbers (S1 round 2, ~2.4-2.5k duels per seat):**
        mage ranker **+5.2pp** over stock (PASS), warrior ranker **-3.7pp** (FAIL).
        The round-3 retrain then regressed *both* seats (-5.7pp / -11.6pp) - the first
        lesson in evaluation honesty, and why S1 froze nothing.
        """
    )
    return


@app.cell
def _(mo):
    n_duels = mo.ui.slider(start=100, stop=5000, step=100, value=400, label="duels per seat")
    n_duels
    return (n_duels,)


@app.cell
def _(alt, mo, np, pd, pal, n_duels):
    _n = n_duels.value
    _seats = [
        ("Warrior (Arms)", 70.9, 74.6),
        ("Mage (Frost)", 31.0, 25.8),
    ]
    _rows = []
    for _seat, _wr, _stock in _seats:
        _hw = 1.96 * np.sqrt((_wr / 100) * (1 - _wr / 100) / _n) * 100
        _rows.append({
            "seat": _seat,
            "uplift": _wr - _stock,
            "lo": _wr - _stock - _hw,
            "hi": _wr - _stock + _hw,
            "hw": _hw,
        })
    _df = pd.DataFrame(_rows)
    _rules = pd.DataFrame({
        "what": ["stock baseline (0)", "gate (+2pp)"],
        "y": [0.0, 2.0],
    })
    _ci = (
        alt.Chart(_df)
        .mark_rule(color=pal["gray"], strokeWidth=2)
        .encode(x=alt.X("seat:N", title=None), y=alt.Y("lo:Q", title="uplift vs own stock baseline (pp)",
                scale=alt.Scale(domain=[-15, 15])), y2="hi:Q")
    )
    _pts = (
        alt.Chart(_df)
        .mark_point(filled=True, size=120, color=pal["blue"])
        .encode(x="seat:N", y="uplift:Q")
    )
    _rl = (
        alt.Chart(_rules)
        .mark_rule(strokeDash=[5, 3], strokeWidth=1.5)
        .encode(y="y:Q", color=alt.Color("what:N",
                scale=alt.Scale(domain=["stock baseline (0)", "gate (+2pp)"],
                                range=[pal["gray"], pal["crit"]]),
                legend=alt.Legend(title=None, orient="bottom")))
    )
    _chart = (_ci + _pts + _rl).properties(width=420, height=280,
        title=f"S1 round-2 uplift with 95% CI at N = {_n} duels/seat")
    _hw0 = _df["hw"].iloc[0]
    _readable = _hw0 < 2.0
    _msg = mo.md(
        f"At **N = {_n}** the error bars are roughly **±{_hw0:.1f}pp**. "
        + ("Small enough to read a ±2pp gate - this is why every gate run farms **~2,400+ duels per seat**."
           if _readable else
           "The ±2pp gate drowns in noise at this N - any verdict here would be a coin flip dressed as science.")
    )
    mo.vstack([_chart, _msg])
    return


@app.cell
def _(mo):
    mo.md(
        """
        ## 3 - Learning to rank actions (S2)

        The ranker idea: score every **legal** action in the current state, take the best.
        Here is the smallest possible version - six warrior actions, one state feature
        (foe health), a logistic ranker trained on simulated win/loss logs.
        Drag the training steps and watch it discover what every warrior knows:
        **Execute is for low health**.

        **Real numbers:** the real action space is the full legal spellbook -
        **49 warrior / 220 mage** actions under a per-tick legality mask (S2), later
        **45 / 218 logits** at M2.
        S2's first honest gate was a disaster - stacked FAIL at **0.0% / 0.1%** - and the
        win-anchored climb to **58.6% / 24.7%** still plateaued: the 70-D features could
        not see past a motionless world.
        """
    )
    return


@app.cell
def _(mo):
    train_steps = mo.ui.slider(start=0, stop=200, step=5, value=0, label="training steps")
    train_steps
    return (train_steps,)


@app.cell
def _(alt, mo, np, pd, pal, train_steps):
    _names = ["Mortal Strike", "Overpower", "Execute", "Rend", "Hamstring", "Heroic Strike"]
    _colors = [pal["blue"], pal["orange"], pal["aqua"], pal["yellow"], pal["magenta"], pal["green"]]
    _rng = np.random.default_rng(7)
    _N = 3000
    _hp = _rng.random(_N)
    _a = _rng.integers(0, 6, _N)
    _base = np.array([0.55, 0.50, 0.25, 0.45, 0.40, 0.42])
    _ptrue = _base[_a].astype(float)
    _ex = _a == 2
    _ptrue[_ex] = 0.85 - 0.60 * _hp[_ex]
    _win = (_rng.random(_N) < _ptrue).astype(float)
    # features: onehot(action) + onehot(action)*foe_hp
    _X = np.zeros((_N, 12))
    _X[np.arange(_N), _a] = 1.0
    _X[np.arange(_N), 6 + _a] = _hp
    _w = np.zeros(12)
    _k = train_steps.value
    for _i in range(_k):
        _s = 1.0 / (1.0 + np.exp(-(_X @ _w)))
        _w += 2.0 * (_X.T @ (_win - _s)) / _N
    _grid = np.linspace(0, 1, 60)
    _rows = []
    for _j, _nm in enumerate(_names):
        _score = 1.0 / (1.0 + np.exp(-(_w[_j] + _w[6 + _j] * _grid)))
        for _g, _sc in zip(_grid, _score):
            _rows.append({"foe health": _g * 100, "predicted winrate": _sc * 100, "action": _nm})
    _df = pd.DataFrame(_rows)
    _chart = (
        alt.Chart(_df)
        .mark_line(strokeWidth=2)
        .encode(
            x=alt.X("foe health:Q", title="foe health (%)"),
            y=alt.Y("predicted winrate:Q", scale=alt.Scale(domain=[0, 100]), title="ranker score (predicted winrate %)"),
            color=alt.Color("action:N", scale=alt.Scale(domain=_names, range=_colors),
                            legend=alt.Legend(title=None, orient="bottom", columns=3)),
        )
        .properties(width=520, height=280, title=f"Ranker after {_k} training steps")
    )
    _spot = 0.15
    _scores = 1.0 / (1.0 + np.exp(-(_w[:6] + _w[6:] * _spot)))
    _order = np.argsort(-_scores)
    _top = _names[int(_order[0])]
    _msg = mo.md(
        f"Spotlight state - **foe at 15% health**: the ranker picks **{_top}** "
        f"({', '.join(f'{_names[_i]} {_scores[_i]*100:.0f}%' for _i in _order[:3])}). "
        + ("Untrained, it knows nothing: every action scores 50%." if _k == 0 else
           "It was never told what Execute does - it read it out of win/loss logs." if _top == "Execute" else
           "Still learning - keep dragging.")
    )
    mo.vstack([_chart, _msg])
    return


@app.cell
def _(mo):
    mo.md(
        """
        ## 4 - Features are the world: state-space explosion (M0/M1)

        Why not just tabulate "in state X, do Y"?
        Count the states.
        Give the bot d features, each coarsely split into just 4 bins, and you get 4^d
        distinct states - drag d and watch the table outgrow every log we ever farmed.

        **Real numbers:** the real state is **continuous**, not binned - **70-D** in the
        statue world (S2), **90-D** when the world started moving (M0/M1: kinematics,
        impairments, 8 terrain probes), **112-D** at M2 (pet + form).
        M0's own gate: movement cost throughput, **84.2%** of control - waived
        (DEC-038) because movement duels are 32% shorter.
        This is why the policy is a neural net, not a lookup table.
        """
    )
    return


@app.cell
def _(mo):
    n_feats = mo.ui.slider(start=1, stop=20, step=1, value=6, label="features tracked (4 bins each)")
    n_feats
    return (n_feats,)


@app.cell
def _(alt, mo, np, pd, pal, n_feats):
    _rows_budget = 4_010_000.0
    _d = np.arange(1, 21)
    _states = 4.0 ** _d
    _df = pd.DataFrame({"features": _d, "states": _states})
    _cur = 4.0 ** n_feats.value
    _line = (
        alt.Chart(_df)
        .mark_line(color=pal["blue"], strokeWidth=2)
        .encode(
            x=alt.X("features:Q", title="features tracked"),
            y=alt.Y("states:Q", scale=alt.Scale(type="log"), title="distinct states (log scale)"),
        )
    )
    _budget = (
        alt.Chart(pd.DataFrame({"y": [_rows_budget], "what": ["4.01M logged decisions (M2 round 0)"]}))
        .mark_rule(strokeDash=[5, 3], strokeWidth=1.5)
        .encode(y="y:Q", color=alt.Color("what:N", scale=alt.Scale(range=[pal["gray"]]),
                legend=alt.Legend(title=None, orient="bottom")))
    )
    _pt = (
        alt.Chart(pd.DataFrame({"features": [n_feats.value], "states": [_cur]}))
        .mark_point(filled=True, size=140, color=pal["orange"])
        .encode(x="features:Q", y="states:Q")
    )
    _chart = (_line + _budget + _pt).properties(width=520, height=280,
        title="4 bins per feature: states vs features")
    _per = _rows_budget / _cur
    _msg = mo.md(
        f"At **{n_feats.value} features**: **{_cur:,.0f}** states, so our entire 4M-row log gives "
        + (f"**{_per:,.0f}** samples per state - a table would work, barely."
           if _per >= 1 else
           f"**{_per:.4f}** samples per state - most states have never been seen once. "
           "At 11 coarse features the table already outgrows the log; the real 112 continuous "
           "dimensions are hopeless. A net generalizes across states instead of memorizing them.")
    )
    mo.vstack([_chart, _msg])
    return


@app.cell
def _(mo):
    mo.md(
        """
        ## 5 - Co-adaptation: training against a moving opponent (M2)

        Everything so far learned against a **fixed** opponent.
        M2 trained both seats against each other - and a policy tuned to beat *this*
        opponent can collapse when the opponent retrains.
        The toy: two players, three styles in a rock-paper-scissors relationship
        (Aggro beats Greedy beats Safe beats Aggro), each in turn switching to the best
        response to the other.
        Drag the rounds - nobody converges, the edge just sloshes back and forth.

        **Real numbers (M2 round trajectories, tau = 0, vs the fresh S0+M1 baseline
        45.9% warrior / 54.6% mage):** see the second chart - warrior peaked at
        round 1 (**58.0%**), mage at round 0 (**75.2%**), and **round 2 regressed both
        seats**.
        The offline gates could not see it; only a winrate eval could - which became the
        DEC-051 deploy discipline.
        """
    )
    return


@app.cell
def _(mo):
    br_rounds = mo.ui.slider(start=1, stop=30, step=1, value=8, label="best-response rounds")
    br_rounds
    return (br_rounds,)


@app.cell
def _(alt, mo, np, pd, pal, br_rounds):
    _styles = ["Aggro", "Greedy", "Safe"]
    # M[i, j] = P(player with style i beats style j)
    _M = np.array([
        [0.50, 0.65, 0.35],
        [0.35, 0.50, 0.65],
        [0.65, 0.35, 0.50],
    ])
    _br = {0: 2, 1: 0, 2: 1}  # best response: what beats style j
    _p1, _p2 = 0, 1
    _rows = [{"round": 0, "P1 winrate": _M[_p1, _p2] * 100, "P1 plays": _styles[_p1], "P2 plays": _styles[_p2]}]
    for _r in range(1, br_rounds.value + 1):
        if _r % 2 == 1:
            _p2 = _br[_p1]
        else:
            _p1 = _br[_p2]
        _rows.append({"round": _r, "P1 winrate": _M[_p1, _p2] * 100,
                      "P1 plays": _styles[_p1], "P2 plays": _styles[_p2]})
    _df = pd.DataFrame(_rows)
    _toy = (
        alt.Chart(_df)
        .mark_line(color=pal["blue"], strokeWidth=2, point=alt.OverlayMarkDef(filled=True, size=60, color=pal["blue"]))
        .encode(
            x=alt.X("round:Q", title="retrain round (players alternate)"),
            y=alt.Y("P1 winrate:Q", scale=alt.Scale(domain=[0, 100]), title="P1 winrate (%)"),
            tooltip=["round", "P1 plays", "P2 plays", "P1 winrate"],
        )
        .properties(width=520, height=200, title="Toy: alternating best response never settles")
    )
    _fifty = (
        alt.Chart(pd.DataFrame({"y": [50.0]}))
        .mark_rule(color=pal["gray"], strokeDash=[5, 3])
        .encode(y="y:Q")
    )
    _last = _df.iloc[-1]
    _real = pd.DataFrame([
        {"seat": "Warrior", "round": 0, "winrate": 33.0, "frozen": False},
        {"seat": "Warrior", "round": 1, "winrate": 58.0, "frozen": True},
        {"seat": "Warrior", "round": 2, "winrate": 46.9, "frozen": False},
        {"seat": "Mage", "round": 0, "winrate": 75.2, "frozen": True},
        {"seat": "Mage", "round": 1, "winrate": 49.7, "frozen": False},
        {"seat": "Mage", "round": 2, "winrate": 35.8, "frozen": False},
    ])
    _base = pd.DataFrame([
        {"seat": "Warrior", "y": 45.9, "what": "Warrior baseline 45.9%"},
        {"seat": "Mage", "y": 54.6, "what": "Mage baseline 54.6%"},
    ])
    _seat_scale = alt.Scale(domain=["Warrior", "Mage"], range=[pal["blue"], pal["orange"]])
    _rlines = (
        alt.Chart(_real)
        .mark_line(strokeWidth=2, point=alt.OverlayMarkDef(filled=True, size=70))
        .encode(
            x=alt.X("round:Q", scale=alt.Scale(domain=[-0.2, 2.2]), axis=alt.Axis(values=[0, 1, 2]), title="training round"),
            y=alt.Y("winrate:Q", scale=alt.Scale(domain=[0, 100]), title="tau=0 winrate (%)"),
            color=alt.Color("seat:N", scale=_seat_scale, legend=alt.Legend(title=None, orient="bottom")),
        )
    )
    _rbase = (
        alt.Chart(_base)
        .mark_rule(strokeDash=[5, 3], strokeWidth=1.5, opacity=0.7)
        .encode(y="y:Q", color=alt.Color("seat:N", scale=_seat_scale, legend=None))
    )
    _rfroz = (
        alt.Chart(_real[_real["frozen"]])
        .mark_text(text="frozen", dy=-14, fontWeight="bold", color=pal["good"])
        .encode(x="round:Q", y="winrate:Q")
    )
    _rchart = (_rlines + _rbase + _rfroz).properties(width=520, height=240,
        title="Real M2: each seat's round trajectory (frozen at its peak)")
    _msg = mo.md(
        f"Toy at round {int(_last['round'])}: P1 plays **{_last['P1 plays']}**, "
        f"P2 plays **{_last['P2 plays']}**, P1 wins **{_last['P1 winrate']:.0f}%** - "
        "and the next retrain flips it again. The real fix was not more rounds: it was "
        "**freezing each seat at its measured peak** and gating every deploy on a real winrate leg."
    )
    mo.vstack([_toy + _fifty, _rchart, _msg])
    return


@app.cell
def _(mo):
    mo.md(
        """
        ## 6 - The full loop: collect, train, gate, freeze - *live moment 3 (finale)*

        Every stage ran the same loop: **collect** duels at high temperature, **train**
        on the logs, **gate** the argmax policy against the baseline with a real winrate
        eval, **freeze** only what passes.
        The toy: five tactics with hidden true winrates, 40 duels per round, win-only
        labels, aggregate everything, gate at **+2pp**.
        Drag the round forward and watch the loop find the good tactic - and get gated
        until it has.

        **Real numbers:** this loop, at scale (farm history **1,698 to 12,351
        duels/hour**, aggregates of **4-6M rows** per training round), produced the
        M2 freeze: warrior **+12.1pp**, mage **+20.6pp** - the first stage where **both
        seats** beat the baseline.
        """
    )
    return


@app.cell
def _(mo):
    loop_round = mo.ui.slider(start=0, stop=8, step=1, value=0, label="loop rounds run")
    loop_round
    return (loop_round,)


@app.cell
def _(alt, mo, np, pd, pal, loop_round):
    _names = ["Turtle", "Poke", "Trade", "Kite", "Burst"]
    _ptrue = np.array([0.35, 0.44, 0.50, 0.55, 0.61])
    _rng = np.random.default_rng(27)  # seed picked for a readable trajectory: climb, dip, recover
    _wins = np.zeros(5)
    _plays = np.zeros(5)
    _hist = []
    _frozen_at = None
    for _r in range(9):
        # eval leg first: argmax of current estimate, tau=0, true winrate
        _est = (_wins + 1.0) / (_plays + 2.0)
        _pick = int(np.argmax(_est))
        _wr = _ptrue[_pick] * 100
        _gate = _wr >= 52.0
        if _gate and _frozen_at is None and _r > 0:
            _frozen_at = _r
        _hist.append({"round": _r, "winrate": _wr, "tactic": _names[_pick],
                      "gate": "PASS" if _gate else "fail", "rows": int(_plays.sum())})
        # collect at high temperature (near-uniform explore), aggregate - never drop rows
        _probs = np.exp(_est * 3.0) / np.exp(_est * 3.0).sum()
        _picks = _rng.choice(5, size=40, p=_probs)
        for _t in range(5):
            _m = int((_picks == _t).sum())
            _plays[_t] += _m
            _wins[_t] += _rng.binomial(_m, _ptrue[_t])
    _df = pd.DataFrame(_hist[: loop_round.value + 1])
    _line = (
        alt.Chart(_df)
        .mark_line(color=pal["blue"], strokeWidth=2, point=alt.OverlayMarkDef(filled=True, size=70, color=pal["blue"]))
        .encode(
            x=alt.X("round:Q", scale=alt.Scale(domain=[-0.2, 8.2]), axis=alt.Axis(values=list(range(9))), title="round"),
            y=alt.Y("winrate:Q", scale=alt.Scale(domain=[30, 70]), title="tau=0 eval winrate (%)"),
            tooltip=["round", "tactic", "winrate", "gate", "rows"],
        )
    )
    _rules = (
        alt.Chart(pd.DataFrame({"y": [50.0, 52.0], "what": ["baseline (50%)", "gate (+2pp)"]}))
        .mark_rule(strokeDash=[5, 3], strokeWidth=1.5)
        .encode(y="y:Q", color=alt.Color("what:N",
                scale=alt.Scale(domain=["baseline (50%)", "gate (+2pp)"], range=[pal["gray"], pal["crit"]]),
                legend=alt.Legend(title=None, orient="bottom")))
    )
    _chart = (_line + _rules).properties(width=520, height=280,
        title=f"The loop after {loop_round.value} round(s)")
    _now = _df.iloc[-1]
    _msg = mo.md(
        f"Round {int(_now['round'])}: policy argmax = **{_now['tactic']}**, eval winrate "
        f"**{_now['winrate']:.0f}%** - gate **{_now['gate']}** "
        f"({_now['rows']:,} duels logged so far). "
        + ("The estimate is still built on nothing - explore first." if loop_round.value == 0 else
           ("**This is the whole curriculum in one chart**: explore, log, retrain, and only "
            "freeze what survives a real eval." if _frozen_at is not None and loop_round.value >= _frozen_at
            else "Not frozen yet - collect more."))
    )
    mo.vstack([_chart, _msg])
    return


@app.cell
def _(mo):
    mo.md(
        """
        ## The real ladder

        Everything above, as it actually went:

        | Stage | What it was | Outcome |
        |---|---|---|
        | S0 | Softmax(tau) over stock scripted scores | The baseline; farm tau=10, demo tau=0 |
        | S1 | Learned ranker, scripted queue | **Soft-fail** - mage +5.2pp, warrior -3.7pp; round 3 regressed both |
        | S2 | Ranker over full spellbook (49/220) | **Soft-fail** - stacked 0.0%/0.1%, climb to 58.6%/24.7%, feature plateau |
        | M0 | Client-authentic movement substrate | **Frozen** - throughput 84.2% waived; the world starts moving |
        | M1 | Learned 9-way movement intent | **Frozen** - mage +2.3pp PASS (kites wider than its teacher); warrior parity waiver |
        | M2 | Ability head co-adapted on the moving world | **Frozen** - warrior **+12.1pp**, mage **+20.6pp**; first both-seat PASS |

        Six stages, two honest failures, three freezes with waivers attached, and one
        loop - collect, train, gate, freeze - that never changed.
        """
    )
    return


if __name__ == "__main__":
    app.run()
