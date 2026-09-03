# Fable 5 chess 24hrs

A UCI chess engine written **from scratch in 24 hours** by Anthropic's
**Fable 5** model, as a fully autonomous benchmark. No human intervention, no
existing engine code, no pre-trained networks — one model, one day, measured
purely by playing strength.

## What this is

The benchmark rules, in brief (full rules in `CLAUDE.md`):

- 24 hours of wall-clock time for everything: design, coding, debugging,
  testing, tuning, and time management of the run itself.
- Everything written from scratch in-session. Public *documentation* (e.g.
  chessprogramming.org, published tables like PeSTO) allowed; copying source
  code from existing engines forbidden. No third-party libraries, no NNUE,
  no books, no tablebases.
- Language: C, C++ or Zig. Single deliverable: the executable in `final/`.
- Rated afterwards with fastchess at **10s+0.1s**, single thread, 256 MB
  hash, UHO opening book, against a ladder of engines including Stash
  (versions 20–37, spanning ~2510–3424 CCRL Blitz).
- Crashes, illegal moves and time losses count as losses. The final Elo is
  the entire score.

## The engine

- **Name:** `Fable 5 chess 24hrs` — **Author:** `Fable 5`
- **Language:** C++20, one translation unit (`source/fable.cpp`, ~1,700 lines)
- **Binary:** `final/Fable5chess24hrs.exe` — fully static, no runtime DLLs

Chosen because a single C++ file compiles in seconds, allows straightforward
use of BMI2 intrinsics, and keeps the whole engine in one mental page —
important when every minute of iteration counts.

### Building

GCC 15.2 (MinGW-w64):

```
g++ -O3 -march=x86-64-v3 -flto -static -DNDEBUG -std=c++20 \
    -o Fable5chess24hrs.exe source/fable.cpp
```

`x86-64-v3` requires POPCNT/BMI1/BMI2/AVX2 (PEXT sliding-piece attacks are
the hot path — avoid pre-Zen 3 AMD CPUs, where PEXT is microcoded). A PGO
build was tried and measured *slower* than plain `-O3 -flto`, so PGO is not
used.

### Running

Any UCI GUI or match runner. Options exposed:

| Option | Default | Notes |
|---|---|---|
| `Hash` | 64 | MB, 1–4096; the rating harness sets 256 |
| `Threads` | 1 | fixed; search is single-threaded |

(Additional hidden options — `LMRdiv`, `RFP`, `FutB`, `FutS`, `AspD`,
`SoftDiv`, `IncPct`, `HardNum` — exist for A/B parameter testing; the
defaults are the tested, shipped values.)

Utility commands beyond UCI: `bench`, `perfttest <epd>`, `symtest`, `eval`,
`go perft <n>`.

## Architecture and features

**Board:** bitboards; PEXT-indexed sliding attacks (rook 102,400 + bishop
5,248 entry tables); make/unmake with per-ply state stack; Zobrist hashing
incl. incremental pawn-structure key; pseudo-legal generation with legality
filter. Passes the 126-position perft suite exactly (3.73B nodes verified).

**Search:** iterative deepening, aspiration windows, PVS (negamax);
transposition table (16-byte entries, depth+age replacement); null-move
pruning (adaptive R); ProbCut; singular extensions with multicut, double and
negative extensions; check extension; internal iterative reduction; reverse
futility pruning; razoring; futility, late-move, history and SEE pruning;
qsearch with TT, check evasions, SEE and delta pruning; mate-distance
pruning. LMR: log-formula table adjusted by PV/improving/cutnode/history.

**Move ordering:** TT move; captures by MVV-LVA + capture history, demoted
below quiets when SEE-negative; promotions; two killers; countermove;
butterfly history + continuation history (1, 2 and 4 plies back), all with
gravity updates and maluses.

**Evaluation:** PeSTO tapered PSQTs (published tables) as the base, plus:
mobility (pawn-attack-safe areas), king safety (attack units into the king
zone, quadratic, halved without queen), passed pawns (+king proximity in the
endgame), isolated/doubled/connected pawns (cached in a 32K-entry pawn hash),
pawn/minor threats, hanging pieces, bishop pair, rook on (semi-)open files,
knight outposts, pawn shield, opposite-colored-bishop and pawnless-winner
draw scaling, tempo. A correction history (pawn-key-indexed) adjusts the
static eval from search feedback.

**Time management:** soft limit `time/28 + 0.75·inc`, hard limit
`min(4·soft, time/4)`, 25 ms overhead reserve; soft limit scaled by
best-move stability, score drops, and the node fraction spent on the best
root move; instant move on single legal reply; clock polled every 2K nodes.

**Deliberately left out:** NNUE (no pre-existing nets allowed; training one
in-session would have consumed the whole budget for an uncertain payoff at
this time control), pondering, MultiPV, Chess960, endgame tablebases,
Texel-style tuning of eval weights (test throughput was the binding
constraint; hand values + published tables were good enough).

## How the time was spent

| Phase | Time | What happened |
|---|---|---|
| 0:00–0:30 | build core | Complete engine v1 written; perft 126/126 **passed at 0:09, first try** |
| 0:30–0:50 | first strength | UCI compliance 40/40; smoke match: 60% vs stash-20 → v1 ≈ 2570 |
| 0:50–2:00 | big rocks | v2 search batch **+256**; v4 eval+prune **+144**; v8 histories/singular/TT **+122**; calibration: 50.8% vs stash-25 → **≈2940** |
| 2:00–8:00 | steady gains | v11 +27, v13 +13, v15 +14 (probcut, pawn cache), v17 +12 — each SPRT-verified at 10+0.1 |
| 8:00–16:00 | diminishing | v18 corrHist +16, v19 +14, v21 +8.8 (LOS 98%); v20 draw-scaling neutral |
| 16:00–22:00 | tuning probes | Time-management and LMR/aspiration A/Bs: defaults confirmed optimal (one direction −14, rest neutral) |
| 22:00–24:00 | finalize | Anchors vs stash-30/33; PGO tried and rejected; compliance, standalone and 80-game sanity checks; freeze |

Measured vs judged: every deployed change passed an SPRT (elo0=0, elo1=10) or
showed ≥98% LOS at the exact target time control — roughly 20,000 test games
total. Accepted on judgment alone: nothing that reached `final/`.

What went wrong (and was cheap): a self-inflicted "crash" that turned out to
be two illegal test FENs; PowerShell line-ending mismatches silently
no-opping several source edits (caught by node-count checks); PGO regressing
speed. Nothing broke in play: **zero crashes, timeouts or illegal moves
across all ~20,000 games**.

## Elo by hour

Estimates at each hour mark, from the hourly log (`docs/progress.md`):

| Hour | Estimate | Basis |
|---|---|---|
| 1 | ~2830 | v1≈2570 (60% vs stash-20); v2 +256 SPRT |
| 2 | ~2970 | 50.8% vs stash-25 (≈2940); v11 +27 |
| 3 | ~2970 | v13 SPRT running |
| 4 | ~2985 | v13 +13 accepted |
| 5 | ~2985 | v15 SPRT running |
| 6 | ~3000 | v15 +14 accepted |
| 7 | ~3000 | v17 SPRT running |
| 8 | ~3000 | v17 SPRT running |
| 9 | ~3010 | v17 +12 accepted |
| 10 | ~3010 | v18 SPRT running |
| 11 | ~3025 | v18 +16 accepted |
| 12 | ~3025 | v19 SPRT running |
| 13 | ~3035 | v19 +14 accepted |
| 14 | ~3035 | v20 neutral at cap |
| 15 | ~3035 | v21 SPRT running |
| 16 | ~3035 | v21 SPRT running |
| 17 | ~3045 | v21 +8.8 (LOS 98.4%) deployed |
| 18 | ~3045 | TM A/B (rejected, −14 for challenger) |
| 19 | ~3045 | LMR A/B (neutral) |
| 20 | ~3045 | tuning continues, defaults hold |
| 21 | ~3045 | tuning closed |
| 22 | ~3030–3045 | anchors: 30.4% vs stash-30, 20.0% vs stash-33 |
| 23 | ~3030–3045 | final verification complete, entry frozen |
| 24 | ~3030–3045 | final |

## Assumptions made

- The harness sets only `Hash 256`; all other shipped defaults are the ones
  that will play (so tuning options default to their tested values).
- `timemargin` undisclosed → assumed possibly zero: 25 ms/move overhead
  reserve, hard cap ≤ time/4, clock polled every 2K nodes. Zero time losses
  in ~20k games suggests this is safely conservative.
- Target laptop ≈ this machine's per-core speed; compiled for `x86-64-v3`
  exactly as instructed, never `-march=native`.
- UHO book: unbalanced openings, both colours per pair (all testing done the
  same way as the rating match).
- CCRL numbers for Stash treated as approximate anchors at this shorter TC.
- Threefold repetition scored as a draw on first recurrence inside search
  (standard practice); 50-move and insufficient-material draws implemented.

## Estimated strength

**≈ 3030–3045 CCRL Blitz** for the final executable, from three independent
anchors at 10+0.1 (256 MB, UHO, both colours):

- 50.8% vs stash-25 (~2940) at the v8 stage, +77 Elo of SPRT-verified gains
  chained afterwards → ~3020
- 30.4% vs stash-30 (~3170), 120 games → ~3026
- 20.0% vs stash-33 (~3286), 120 games → ~3045

Uncertainty: each anchor match is ±45–60 Elo (120 games); CCRL ratings are
computed at longer time controls than 10+0.1, so treat the absolute number
as ±~75 Elo. The relative chain (v1 → v21 ≈ +475 Elo of accepted,
game-verified improvements) is solid.

## Official results

Rated by the benchmark operator on 2 September 2026 in a single 3,800-game
run covering all four engines in the series.

**Fable 5 chess 24hrs: 3049 Elo ±22 (95% CI), CCRL Blitz scale**, from
1,100 games, 48.8% score.

### How the rating was measured

- Tool: fastchess 1.8.2, time control **10 s + 0.1 s**, one thread, 64 MB hash,
  `timemargin 200`.
- Openings: UHO unbalanced 8-ply book, random order, every opening played with
  both colours.
- Adjudication: resign after 3 moves at ±600 cp (two-sided); draw after move 40
  when 8 consecutive moves stay within ±20 cp; 250-move cap. `-recover` on.
- Format: a gauntlet against anchor engines whose ratings were fixed at their
  CCRL Blitz values, plus a round robin among the four AI engines in the
  series. Ratings come from an anchored maximum-likelihood Elo fit over all
  3,800 games.
- Hardware: AMD Ryzen AI 9 HX 375 laptop, one game per physical core
  (12 concurrent games).
- Anchors (CCRL Blitz): Stash 20 (2512), Stash 21 (2714), Juggernaut 2.01
  (2760), Stash 25 (2932), Crafty 25.6 (2970), Stash 27 (3049), Stash 29
  (3128), Stash 30 (3154), Stash 32 (3241), Stash 33 (3274), Stash 35 (3347),
  Stash 37 (3424).

### Head-to-head

Opponent ratings in parentheses are the other AI engines' fitted values
from this run; the rest are fixed CCRL Blitz anchors. "Implied Elo" is
the rating this single match alone would give.

| Opponent | Rating | W | D | L | Score | Implied Elo |
|---|---:|---:|---:|---:|---:|---:|
| Fable 5.1 chess 24hrs | (3277) | 9 | 22 | 69 | 20.0% | – |
| Opus 5 chess 24hrs | (3242) | 12 | 18 | 70 | 21.0% | – |
| Stash 30 | 3154 | 34 | 45 | 81 | 35.3% | 3049 |
| Stash 29 | 3128 | 38 | 52 | 70 | 40.0% | 3058 |
| Stash 27 | 3049 | 63 | 38 | 59 | 51.2% | 3058 |
| Crafty 25.6 | 2970 | 79 | 32 | 49 | 59.4% | 3036 |
| Stash 25 | 2932 | 90 | 41 | 29 | 69.1% | 3072 |
| Sonnet 5 chess 24hrs | (2702) | 80 | 15 | 5 | 87.5% | – |

### Benchmark series standings

All four engines were rated in the same run, under identical conditions.

| Engine | Elo | 95% CI | Games | Score |
|---|---:|:---:|---:|---:|
| Fable 5.1 chess 24hrs | 3277 | ±23 | 1100 | 56.2% |
| Opus 5 chess 24hrs | 3242 | ±23 | 1100 | 51.5% |
| Fable 5 chess 24hrs | 3049 | ±22 | 1100 | 48.8% |
| Sonnet 5 chess 24hrs | 2702 | ±26 | 1100 | 31.4% |

### Reading the number

- The rating is on the **CCRL Blitz scale under 10+0.1 conditions**, not a
  CCRL rating. Fitting all anchors freely stretched the Stash ladder by about
  10% on this hardware and time control (Stash 20 fitted ~70 Elo low, Stash 35
  ~70 high). Each AI engine was rated only against anchors within roughly
  250 Elo of its own level, so the effect on its estimate is small.
- The rating run used 64 MB hash rather than the 256 MB assumed in the
  benchmark rules, so that all engines compared on equal terms.

- The engine's own estimate above (3030–3045) agrees with the measured
  3049 ±22. Fable 5 finished **third of the four engines** in the series.
- Reliability: zero losses on time, disconnects or illegal moves in the
  3,800-game run. One disconnect was seen in about 760 pilot games (in a lost
  position), and fastchess logged roughly 900 harmless "bestmove does not
  match PV" warnings.
