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

## Firsts
- **Full perft suite passed: 2026-08-22 ~14:15 (elapsed ~0:09).** 126/126
  positions, 3.73B nodes, 52 Mnps (O2 build), zero failures, first attempt.
- **First full legal games: 2026-08-22 ~14:23** (60-game smoke match vs
  stash-20 at 10+0.1: 60% score, +70 Elo, 0 crashes/timeouts/illegal moves).

## Hour 1 — 2026-08-22 15:00 (approx, written 14:50)
- Wrote complete v1 engine in C++ (single file ~1100 lines): bitboards, PEXT
  sliders, pseudo-legal movegen + legality filter, Zobrist, make/unmake,
  PeSTO tapered eval, PVS search with TT/null-move/LMR/killers/history/
  aspiration, qsearch with check evasions, time management, UCI with reader
  thread.
- Perft suite: 126/126 pass. fastchess --compliance: all 40 checks pass.
- Static -O3 -flto build in final/ (Fable5chess24hrs.exe). ~4.5 Mnps search.
- Fixed: castleMask init missing; premature quit race in cmd queue.
- Next: smoke match vs stash-20 at 10+0.1 to check stability + clock handling,
  then estimate Elo and start strength iterations (SEE, TT in qsearch, better
  pruning, eval terms).
