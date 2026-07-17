# Stock↔stock baseline (S0 Softmax-stock)

Source: `C:\AzerothCore-server\ml_decisions_duel_v3.csv`  
Eval: `tools/ml/eval_duel_winrate.py` on 2026-07-17

| Seat | Winrate | W | L | Matches |
|------|--------:|--:|--:|--------:|
| Warrior (Arms) | 74.6% | 33830 | 11524 | 45354 |
| Mage (Frost) | 25.8% | 11652 | 33483 | 45135 |

Freeze gate (DEC-025): both seats must clear **this baseline + δ** in mixed seats
(Arms-ranker↔Frost-stock and Arms-stock↔Frost-ranker), not raw 50%.
