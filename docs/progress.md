# Progress log — Fable 5 chess 24hrs

Start: 2026-08-22T14:06:25 (local). Deadline: 2026-08-23T14:06:25.

## Plan (hour 0)
- Language: C++ (g++ 15.2, single translation unit). Bitboards + PEXT sliders
  (target guarantees fast BMI2). Pseudo-legal movegen + legality filter.
- H0–3: board, movegen, perft suite passing.
- H3–6: UCI + iterative-deepening PVS search, TT, PeSTO tapered eval, time
  management. First stable build into final/.
- H6–20: strength work, measured with fastchess vs own versions + Stash ladder.
- H20–24: final build (static, x86-64-v3, -O3/LTO), compliance check, sanity
  match, install into final/.

Assumptions noted as they arise. All decisions recorded here hourly.
