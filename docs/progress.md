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

## Hour 1 — 2026-08-22 15:10
- SPRT v4 vs v2: **+144 Elo** (69.6%, 250 games, accepted). Deployed v4 to
  final/, then SPRT v8 vs v4: **+122 Elo** (66.9%, 258 games, accepted).
  Deployed v8 to final/. final/ is compliance-checked lineage, zero timeouts.
- Implemented while tests ran: v9 (hidden UCI tuning options), v10 (capture
  history, passer king proximity eg, score-drop TM extension), v11 (cutnode
  LMR). All committed, sanity-checked, untested.
- Calibration match v8 vs stash-25 (~2940 CCRL) RUNNING (120 games).
- Next hour: calibration result → SPRT v11 vs v8 → deploy if pass; start
  parameter tuning A/B tests (LMR divisor, time mgmt) using option overrides.
- Elo estimate: ~2950±100 (chained SPRT sum from stash-20 anchor; calibration
  match will give a direct reading).

## Hour 2 — 2026-08-22 16:12
- **Calibration: v8 scored 50.8% vs stash-25 → ≈2940 CCRL Blitz.**
- SPRT v11 vs v8: **+27 Elo** (938 games, accepted). v11 deployed to final/.
- SPRT v13 (connected pawns, minor-on-major threats) vs v11: RUNNING.
- Implemented meanwhile: v14 probcut; v15 pawn-structure cache (verified
  eval-identical via equal bench node count) + history arrays 2048 (safety
  for very long games); sprt.ps1 now uses draw/resign adjudication to speed
  tests.
- Gains per feature are shrinking (normal); tests lengthening (~50 min).
  Next: v13 result → test (v14+v15) vs winner → tuning A/B via UCI options.
- Elo estimate: ~2970 (stash-25 anchor + v11 gain).

## Hours 3–6 — 2026-08-22 20:10 (backfilled; long SPRTs dominated wall-clock)
- SPRT v13 (connected pawns, minor-on-major threats) vs v11: **+13 Elo**
  (2356 games, 2h08m, accepted). Deployed to final/.
- SPRT v15 (probcut + pawn-structure cache) vs v13: **+14 Elo** (2188 games,
  1h52m, accepted). Deployed to final/. Bench on idle cores: 2.75 Mnps,
  probcut cut bench tree ~30%.
- sprt.ps1 now adjudicates (draw movenumber=40 movecount=8 score=10; resign
  score=500) to shorten tests.
- Implemented during waits: v16 (capture LMR, history pruning, node-fraction
  time mgmt), v17 (attack-unit king safety), v18 (correction history).
- SPRT v17 vs v15 RUNNING (v16+v17 bundle). v18 queued next.
- Test cadence is now the limiter (~2h per +13-ish step). ~18h remain;
  budget: 6-8 more feature/tuning tests, stash-30 calibration around hour 16,
  PGO'd final build + verification reserved for the last 2 hours.
- Elo estimate: ~2995 (chain: 2940 anchor +27 +13 +14).

## Hour 7 - 2026-08-22 21:04
- SPRT v17 (capture LMR, history pruning, node-fraction TM, attack-unit king safety) vs v15 still running (~1h).
- v18 (correction history) built, queued next. No other changes; keeping lineage stable while tests decide.
- Elo estimate: ~2995 (unchanged; awaiting v17 result).

## Hour 8 - 2026-08-22 21:59
- SPRT v17 vs v15 still running (1h52m). Waiting; v18 queued.
- Elo estimate: ~2995 (awaiting v17 result).

## Hour 9 - 2026-08-22 22:54
- SPRT v18 (correction history) vs v17 running. v19 (singular double/neg ext, hanging pieces) built+committed, queued.
- Elo estimate: ~3005 (2940 anchor +27+13+14+12).

## Hour 10 - 2026-08-22 23:49
- SPRT v18 (corrHist) vs v17 still running (~1h40m). v19 queued.
- Elo estimate: ~3005.

## Hour 11 - 2026-08-23 00:44
- v18 corrHist accepted earlier: +16 (1830 games). Deployed to final/.
- SPRT v19 (singular double/neg ext, hanging pieces) vs v18 running.
- v20 built+committed: OCB/pawnless draw scaling, single-reply instant move, root-mate fix.
- Elo estimate: ~3020 (chain +16 on 3005).

## Hour 12 - 2026-08-23 01:39
- SPRT v19 vs v18 still running (~1h50m). v20 queued.
- Elo estimate: ~3020.

## Hour 13 - 2026-08-23 02:34
- v19 accepted earlier: +14 (2110 games). Deployed. SPRT v20 (draw scaling, single-reply, root-mate fix) vs v19 running.
- v21 built+committed: singular depth>=6, 4-ply contHist.
- Elo estimate: ~3035.

## Hour 14 - 2026-08-23 03:29
- SPRT v20 vs v19 still running (~1h30m).
- Elo estimate: ~3035.

## Hour 14.5 - 2026-08-23 04:19
- SPRT v20 vs v19: inconclusive at 3000-game cap (+2.0 +/-7.6, LOS 69%). Not deployed; v19 stays baseline.
- SPRT v21 (v20 changes + singular depth 6 + 4-ply contHist) vs v19: RUNNING.
- Remaining plan: v21 result (~2h) -> TM tuning A/B or stash-30 calibration -> PGO final build (last 2h reserved).
- Elo estimate: ~3035.

## Hour 15 - 2026-08-23 04:24
- SPRT v21 vs v19 running (~1h).
- Elo estimate: ~3035.

## Hour 16 - 2026-08-23 05:19
- SPRT v21 vs v19 still running (~2h).
- Elo estimate: ~3035.

## Hour 17 - 2026-08-23 06:14
- SPRT v21 vs v19 at ~2h20m, likely heading to cap (small gain either way).
- Plan for remaining ~8h: v21 result -> TM tuning A/B (SoftDiv/IncPct) -> LMR tuning if time -> stash-30 calibration ~11:00 -> final PGO build + compliance + sanity from 12:00.
- Elo estimate: ~3035.

## Hour 18 - 2026-08-23 06:45
- v21 vs v19: +8.8 +/-8.1 at 3000-game cap, LOS 98.4%. Deployed to final/ (also folds in v20 robustness fixes).
- TM tuning A/B running: SoftDiv 22 + IncPct 85 vs defaults 28/75 (same binary, option override), capped 2000 games.
- After: stash-30 calibration, PGO final build, compliance + sanity match.
- Elo estimate: ~3045.
