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

## Elapsed 0:45 — 2026-08-22 14:51
- Wrote v1 (full engine) in first ~25 min; perft 126/126 at ~14:15; smoke match
  60% vs stash-20 (~2510 CCRL) → v1 ≈ 2570.
- SPRT v2 (SEE, qsearch TT, futility, IIR, improving, countermove, hist malus)
  vs v1: **+256 Elo** (81%, 188 games, H1 accepted). Current ≈ ~2800+.
- Built and committed further batches while tests run:
  v4 = eval (passers/mobility/king safety/pawn struct/bishop pair/rook files)
     + SEE & delta pruning + stability time mgmt (SPRT vs v2 RUNNING)
  v5 = continuation history (1+2 ply), gravity updates
  v6 = singular extensions + multicut + TT prefetch
  v7 = TT eval store/reuse, razoring, adaptive null R, TT aging
- Eval symmetry test: 0 asymmetries. All builds sanity-checked.
- Plan: when v4-v2 SPRT ends → SPRT v7 vs v4 (bundle). Then eval batch 8
  (threats, outposts), speed work, PGO final build.
- Elo estimate: ~2800 (evidence: chained SPRTs from stash-20 anchor).
