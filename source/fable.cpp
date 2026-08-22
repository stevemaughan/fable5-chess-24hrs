// Fable 5 chess 24hrs — UCI chess engine written from scratch.
// Author: Fable 5 (Anthropic). 24-hour benchmark build.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <deque>
#include <sstream>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <immintrin.h>

typedef uint64_t U64;
using namespace std;

// ------------------------------- basics ------------------------------------
enum { WHITE, BLACK };
enum { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, NO_TYPE };
enum { WP, WN, WB, WR, WQ, WK, BP, BN, BB, BR, BQ, BK, NO_PIECE };

static inline int lsb(U64 b) { return (int)_tzcnt_u64(b); }
static inline int poplsb(U64& b) { int s = lsb(b); b &= b - 1; return s; }
static inline int popcnt(U64 b) { return (int)__builtin_popcountll(b); }

static const U64 FILE_A = 0x0101010101010101ULL;
static const U64 FILE_H = 0x8080808080808080ULL;
static const U64 RANK_1 = 0xFFULL;
static const U64 RANK_2 = 0xFF00ULL;
static const U64 RANK_7 = 0xFF000000000000ULL;
static const U64 RANK_8 = 0xFF00000000000000ULL;

#define SQ(f, r) (((r) << 3) | (f))

// ------------------------------- moves -------------------------------------
// 16-bit move: bits 0-5 from, 6-11 to, 12-13 promo piece (0=N..3=Q), 14-15 type
enum { MT_NORMAL = 0, MT_PROMO = 1 << 14, MT_EP = 2 << 14, MT_CASTLE = 3 << 14 };
typedef uint16_t Move;
static inline Move mkMove(int from, int to, int type = MT_NORMAL, int promo = 0) {
    return (Move)(from | (to << 6) | (promo << 12) | type);
}
static inline int moveFrom(Move m) { return m & 63; }
static inline int moveTo(Move m) { return (m >> 6) & 63; }
static inline int movePromo(Move m) { return ((m >> 12) & 3) + KNIGHT; }
static inline int moveType(Move m) { return m & (3 << 14); }

static string sqName(int s) {
    string r; r += (char)('a' + (s & 7)); r += (char)('1' + (s >> 3)); return r;
}
static string moveUci(Move m) {
    if (!m) return "0000";
    string s = sqName(moveFrom(m)) + sqName(moveTo(m));
    if (moveType(m) == MT_PROMO) s += "nbrq"[(m >> 12) & 3];
    return s;
}

struct MoveEntry { Move move; int score; };
struct MoveList {
    MoveEntry m[256];
    int n = 0;
    void add(Move mv) { m[n++].move = mv; }
};

// ---------------------------- attack tables --------------------------------
static U64 pawnAtt[2][64], knightAtt[64], kingAtt[64];
static U64 rookMask[64], bishopMask[64];
static U64* rookPtr[64];
static U64* bishopPtr[64];
static U64 rookTable[102400];
static U64 bishopTable[5248];

static U64 slideAtt(int sq, U64 occ, const int df[4], const int dr[4]) {
    U64 att = 0;
    int f0 = sq & 7, r0 = sq >> 3;
    for (int d = 0; d < 4; d++) {
        int f = f0 + df[d], r = r0 + dr[d];
        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
            att |= 1ULL << SQ(f, r);
            if (occ & (1ULL << SQ(f, r))) break;
            f += df[d]; r += dr[d];
        }
    }
    return att;
}

static void initAttacks() {
    const int rdf[4] = { 1,-1,0,0 }, rdr[4] = { 0,0,1,-1 };
    const int bdf[4] = { 1,1,-1,-1 }, bdr[4] = { 1,-1,1,-1 };
    for (int s = 0; s < 64; s++) {
        int f = s & 7, r = s >> 3;
        U64 b = 1ULL << s;
        pawnAtt[WHITE][s] = ((b & ~FILE_A) << 7) | ((b & ~FILE_H) << 9);
        pawnAtt[BLACK][s] = ((b & ~FILE_A) >> 9) | ((b & ~FILE_H) >> 7);
        U64 n = 0, k = 0;
        const int nf[8] = { 1,2,2,1,-1,-2,-2,-1 }, nr[8] = { 2,1,-1,-2,-2,-1,1,2 };
        for (int i = 0; i < 8; i++) {
            int ff = f + nf[i], rr = r + nr[i];
            if (ff >= 0 && ff < 8 && rr >= 0 && rr < 8) n |= 1ULL << SQ(ff, rr);
        }
        for (int df = -1; df <= 1; df++) for (int dr = -1; dr <= 1; dr++) {
            if (!df && !dr) continue;
            int ff = f + df, rr = r + dr;
            if (ff >= 0 && ff < 8 && rr >= 0 && rr < 8) k |= 1ULL << SQ(ff, rr);
        }
        knightAtt[s] = n; kingAtt[s] = k;
        // masks exclude board edges (in each ray direction, drop the last square)
        U64 edges = ((RANK_1 | RANK_8) & ~(0xFFULL << (8 * r))) | ((FILE_A | FILE_H) & ~(FILE_A << f));
        rookMask[s] = slideAtt(s, 0, rdf, rdr) & ~edges;
        bishopMask[s] = slideAtt(s, 0, bdf, bdr) & ~edges;
    }
    U64* rp = rookTable; U64* bp = bishopTable;
    for (int s = 0; s < 64; s++) {
        rookPtr[s] = rp;
        U64 occ = 0;
        do {
            rp[_pext_u64(occ, rookMask[s])] = slideAtt(s, occ, rdf, rdr);
            occ = (occ - rookMask[s]) & rookMask[s];
        } while (occ);
        rp += 1ULL << popcnt(rookMask[s]);
        bishopPtr[s] = bp;
        occ = 0;
        do {
            bp[_pext_u64(occ, bishopMask[s])] = slideAtt(s, occ, bdf, bdr);
            occ = (occ - bishopMask[s]) & bishopMask[s];
        } while (occ);
        bp += 1ULL << popcnt(bishopMask[s]);
    }
}
static inline U64 rookAtt(int s, U64 occ) { return rookPtr[s][_pext_u64(occ, rookMask[s])]; }
static inline U64 bishopAtt(int s, U64 occ) { return bishopPtr[s][_pext_u64(occ, bishopMask[s])]; }
static inline U64 queenAtt(int s, U64 occ) { return rookAtt(s, occ) | bishopAtt(s, occ); }

// ------------------------------- zobrist ------------------------------------
static U64 zPiece[12][64], zCastle[16], zEp[8], zSide;
static U64 rngState = 0x9E3779B97F4A7C15ULL;
static U64 rand64() {
    U64 z = (rngState += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static void initZobrist() {
    for (int p = 0; p < 12; p++) for (int s = 0; s < 64; s++) zPiece[p][s] = rand64();
    for (int i = 0; i < 16; i++) zCastle[i] = rand64();
    for (int i = 0; i < 8; i++) zEp[i] = rand64();
    zSide = rand64();
}

// ------------------------------- position -----------------------------------
struct StateInfo {
    U64 key;
    int castling;
    int ep;          // ep square or -1
    int fifty;
    int captured;    // piece index or NO_PIECE
};

static int castleMask[64];
static void initCastleMask() {
    for (int i = 0; i < 64; i++) castleMask[i] = 15;
    castleMask[0] = 13;  castleMask[4] = 12;  castleMask[7] = 14;   // a1 e1 h1
    castleMask[56] = 7;  castleMask[60] = 3;  castleMask[63] = 11;  // a8 e8 h8
}

struct Position {
    U64 byType[6] = {};
    U64 byColor[2] = {};
    uint8_t board[64];
    int stm = WHITE;
    int castling = 0;
    int ep = -1;
    int fifty = 0;
    U64 key = 0;
    int histPly = 0;               // index into stack/keyHist
    StateInfo stack[1024];
    U64 keyHist[1024];

    U64 occ() const { return byColor[0] | byColor[1]; }
    U64 pieces(int c, int t) const { return byColor[c] & byType[t]; }
    int kingSq(int c) const {
#ifdef DEBUGCHK
        if (!pieces(c, KING)) { fprintf(stderr, "KING GONE c=%d histPly=%d\n", c, histPly); fflush(stderr); *(volatile int*)0 = 0; }
#endif
        return lsb(pieces(c, KING));
    }

    void put(int pc, int s) {
        board[s] = (uint8_t)pc;
        byColor[pc / 6] |= 1ULL << s;
        byType[pc % 6] |= 1ULL << s;
    }
    void remove(int pc, int s) {
        board[s] = NO_PIECE;
        byColor[pc / 6] &= ~(1ULL << s);
        byType[pc % 6] &= ~(1ULL << s);
    }

    bool attacked(int s, int by) const {
        U64 o = occ();
        if (pawnAtt[by ^ 1][s] & pieces(by, PAWN)) return true;
        if (knightAtt[s] & pieces(by, KNIGHT)) return true;
        if (kingAtt[s] & pieces(by, KING)) return true;
        if (bishopAtt(s, o) & (pieces(by, BISHOP) | pieces(by, QUEEN))) return true;
        if (rookAtt(s, o) & (pieces(by, ROOK) | pieces(by, QUEEN))) return true;
        return false;
    }
    bool inCheck() const { return attacked(kingSq(stm), stm ^ 1); }

    void setFen(const string& fen) {
        memset(byType, 0, sizeof(byType));
        memset(byColor, 0, sizeof(byColor));
        for (int i = 0; i < 64; i++) board[i] = NO_PIECE;
        castling = 0; ep = -1; fifty = 0; histPly = 0;
        istringstream ss(fen);
        string b, side, cast, eps; int half = 0, full = 1;
        ss >> b >> side >> cast >> eps >> half >> full;
        int f = 0, r = 7;
        for (char c : b) {
            if (c == '/') { f = 0; r--; }
            else if (c >= '1' && c <= '8') f += c - '0';
            else {
                const char* pcs = "PNBRQKpnbrqk";
                const char* p = strchr(pcs, c);
                if (p) put((int)(p - pcs), SQ(f, r));
                f++;
            }
        }
        stm = (side == "b") ? BLACK : WHITE;
        for (char c : cast) {
            if (c == 'K') castling |= 1;
            else if (c == 'Q') castling |= 2;
            else if (c == 'k') castling |= 4;
            else if (c == 'q') castling |= 8;
        }
        if (eps.size() == 2 && eps != "--") ep = SQ(eps[0] - 'a', eps[1] - '1');
        fifty = half;
        key = computeKey();
        keyHist[0] = key;
    }

    U64 computeKey() const {
        U64 k = 0;
        for (int s = 0; s < 64; s++)
            if (board[s] != NO_PIECE) k ^= zPiece[board[s]][s];
        k ^= zCastle[castling];
        if (ep >= 0) k ^= zEp[ep & 7];
        if (stm == BLACK) k ^= zSide;
        return k;
    }

    void makeMove(Move m) {
        StateInfo& st = stack[histPly];
        st.key = key; st.castling = castling; st.ep = ep; st.fifty = fifty;
        int from = moveFrom(m), to = moveTo(m), type = moveType(m);
        int us = stm, them = us ^ 1;
        int pc = board[from];
        int captured = (type == MT_EP) ? (them * 6 + PAWN) : board[to];
        st.captured = captured;

        fifty++;
        if (pc % 6 == PAWN || captured != NO_PIECE) fifty = 0;
        if (ep >= 0) { key ^= zEp[ep & 7]; ep = -1; }

        if (captured != NO_PIECE) {
            int capSq = (type == MT_EP) ? (us == WHITE ? to - 8 : to + 8) : to;
            remove(captured, capSq);
            key ^= zPiece[captured][capSq];
        }
        remove(pc, from);
        key ^= zPiece[pc][from];
        int placed = (type == MT_PROMO) ? (us * 6 + movePromo(m)) : pc;
        put(placed, to);
        key ^= zPiece[placed][to];

        if (type == MT_CASTLE) {
            int rf, rt;
            if (to > from) { rf = to + 1; rt = to - 1; }      // O-O
            else { rf = to - 2; rt = to + 1; }                 // O-O-O
            int rook = us * 6 + ROOK;
            remove(rook, rf); put(rook, rt);
            key ^= zPiece[rook][rf] ^ zPiece[rook][rt];
        }
        if (pc % 6 == PAWN && (to - from == 16 || from - to == 16)) {
            ep = (from + to) / 2;
            key ^= zEp[ep & 7];
        }
        key ^= zCastle[castling];
        castling &= castleMask[from] & castleMask[to];
        key ^= zCastle[castling];

        stm = them;
        key ^= zSide;
        histPly++;
        keyHist[histPly] = key;
    }

    void unmakeMove(Move m) {
        histPly--;
        const StateInfo& st = stack[histPly];
        int from = moveFrom(m), to = moveTo(m), type = moveType(m);
        stm ^= 1;
        int us = stm;
        int pc = (type == MT_PROMO) ? (us * 6 + PAWN) : board[to];
        remove(board[to], to);
        put(pc, from);
        if (st.captured != NO_PIECE) {
            int capSq = (type == MT_EP) ? (us == WHITE ? to - 8 : to + 8) : to;
            put(st.captured, capSq);
        }
        if (type == MT_CASTLE) {
            int rf, rt;
            if (to > from) { rf = to + 1; rt = to - 1; }
            else { rf = to - 2; rt = to + 1; }
            int rook = us * 6 + ROOK;
            remove(rook, rt); put(rook, rf);
        }
        key = st.key; castling = st.castling; ep = st.ep; fifty = st.fifty;
    }

    void makeNull() {
        StateInfo& st = stack[histPly];
        st.key = key; st.castling = castling; st.ep = ep; st.fifty = fifty;
        st.captured = NO_PIECE;
        if (ep >= 0) { key ^= zEp[ep & 7]; ep = -1; }
        stm ^= 1;
        key ^= zSide;
        fifty++;
        histPly++;
        keyHist[histPly] = key;
    }
    void unmakeNull() {
        histPly--;
        const StateInfo& st = stack[histPly];
        stm ^= 1;
        key = st.key; castling = st.castling; ep = st.ep; fifty = st.fifty;
    }

    void genMoves(MoveList& list, bool capsOnly) const {
        int us = stm, them = us ^ 1;
        U64 o = occ(), own = byColor[us], enemy = byColor[them];
        U64 targets = capsOnly ? enemy : ~own;
        U64 pawns = pieces(us, PAWN);

        if (us == WHITE) {
            U64 single = (pawns << 8) & ~o;
            if (!capsOnly) {
                U64 dbl = ((single & (RANK_2 << 8)) << 8) & ~o;
                U64 s2 = single & ~RANK_8;
                while (s2) { int t = poplsb(s2); list.add(mkMove(t - 8, t)); }
                while (dbl) { int t = poplsb(dbl); list.add(mkMove(t - 16, t)); }
            }
            U64 promoPush = single & RANK_8;
            while (promoPush) {
                int t = poplsb(promoPush);
                for (int p = 3; p >= 0; p--) list.add(mkMove(t - 8, t, MT_PROMO, p));
            }
            U64 capL = ((pawns & ~FILE_A) << 7) & enemy;
            U64 capR = ((pawns & ~FILE_H) << 9) & enemy;
            U64 cl = capL & ~RANK_8, cr = capR & ~RANK_8;
            while (cl) { int t = poplsb(cl); list.add(mkMove(t - 7, t)); }
            while (cr) { int t = poplsb(cr); list.add(mkMove(t - 9, t)); }
            U64 pl = capL & RANK_8, pr = capR & RANK_8;
            while (pl) { int t = poplsb(pl); for (int p = 3; p >= 0; p--) list.add(mkMove(t - 7, t, MT_PROMO, p)); }
            while (pr) { int t = poplsb(pr); for (int p = 3; p >= 0; p--) list.add(mkMove(t - 9, t, MT_PROMO, p)); }
            if (ep >= 0) {
                U64 att = pawnAtt[BLACK][ep] & pawns;   // pawns that can capture to ep
                while (att) { int f = poplsb(att); list.add(mkMove(f, ep, MT_EP)); }
            }
        } else {
            U64 single = (pawns >> 8) & ~o;
            if (!capsOnly) {
                U64 dbl = ((single & (RANK_7 >> 8)) >> 8) & ~o;
                U64 s2 = single & ~RANK_1;
                while (s2) { int t = poplsb(s2); list.add(mkMove(t + 8, t)); }
                while (dbl) { int t = poplsb(dbl); list.add(mkMove(t + 16, t)); }
            }
            U64 promoPush = single & RANK_1;
            while (promoPush) {
                int t = poplsb(promoPush);
                for (int p = 3; p >= 0; p--) list.add(mkMove(t + 8, t, MT_PROMO, p));
            }
            U64 capL = ((pawns & ~FILE_A) >> 9) & enemy;
            U64 capR = ((pawns & ~FILE_H) >> 7) & enemy;
            U64 cl = capL & ~RANK_1, cr = capR & ~RANK_1;
            while (cl) { int t = poplsb(cl); list.add(mkMove(t + 9, t)); }
            while (cr) { int t = poplsb(cr); list.add(mkMove(t + 7, t)); }
            U64 pl = capL & RANK_1, pr = capR & RANK_1;
            while (pl) { int t = poplsb(pl); for (int p = 3; p >= 0; p--) list.add(mkMove(t + 9, t, MT_PROMO, p)); }
            while (pr) { int t = poplsb(pr); for (int p = 3; p >= 0; p--) list.add(mkMove(t + 7, t, MT_PROMO, p)); }
            if (ep >= 0) {
                U64 att = pawnAtt[WHITE][ep] & pawns;
                while (att) { int f = poplsb(att); list.add(mkMove(f, ep, MT_EP)); }
            }
        }

        U64 kn = pieces(us, KNIGHT);
        while (kn) {
            int f = poplsb(kn);
            U64 a = knightAtt[f] & targets;
            while (a) { int t = poplsb(a); list.add(mkMove(f, t)); }
        }
        U64 bi = pieces(us, BISHOP);
        while (bi) {
            int f = poplsb(bi);
            U64 a = bishopAtt(f, o) & targets;
            while (a) { int t = poplsb(a); list.add(mkMove(f, t)); }
        }
        U64 ro = pieces(us, ROOK);
        while (ro) {
            int f = poplsb(ro);
            U64 a = rookAtt(f, o) & targets;
            while (a) { int t = poplsb(a); list.add(mkMove(f, t)); }
        }
        U64 qu = pieces(us, QUEEN);
        while (qu) {
            int f = poplsb(qu);
            U64 a = queenAtt(f, o) & targets;
            while (a) { int t = poplsb(a); list.add(mkMove(f, t)); }
        }
        int ks = kingSq(us);
        U64 ka = kingAtt[ks] & targets;
        while (ka) { int t = poplsb(ka); list.add(mkMove(ks, t)); }

        if (!capsOnly) {
            if (us == WHITE) {
                if ((castling & 1) && !(o & 0x60ULL)
                    && !attacked(4, BLACK) && !attacked(5, BLACK) && !attacked(6, BLACK))
                    list.add(mkMove(4, 6, MT_CASTLE));
                if ((castling & 2) && !(o & 0x0EULL)
                    && !attacked(4, BLACK) && !attacked(3, BLACK) && !attacked(2, BLACK))
                    list.add(mkMove(4, 2, MT_CASTLE));
            } else {
                if ((castling & 4) && !(o & 0x6000000000000000ULL)
                    && !attacked(60, WHITE) && !attacked(61, WHITE) && !attacked(62, WHITE))
                    list.add(mkMove(60, 62, MT_CASTLE));
                if ((castling & 8) && !(o & 0x0E00000000000000ULL)
                    && !attacked(60, WHITE) && !attacked(59, WHITE) && !attacked(58, WHITE))
                    list.add(mkMove(60, 58, MT_CASTLE));
            }
        }
    }

    // did the move we just made leave OUR king (previous stm) in check?
    bool lastMoveIllegal() const { return attacked(kingSq(stm ^ 1), stm); }

    U64 attackersTo(int s, U64 o) const {
        return (pawnAtt[WHITE][s] & pieces(BLACK, PAWN))
             | (pawnAtt[BLACK][s] & pieces(WHITE, PAWN))
             | (knightAtt[s] & byType[KNIGHT])
             | (kingAtt[s] & byType[KING])
             | (bishopAtt(s, o) & (byType[BISHOP] | byType[QUEEN]))
             | (rookAtt(s, o) & (byType[ROOK] | byType[QUEEN]));
    }
};

// static exchange evaluation (swap algorithm)
static const int seeVal[7] = { 100, 320, 330, 500, 950, 20000, 0 };
static int see(const Position& pos, Move m) {
    int to = moveTo(m), from = moveFrom(m);
    int gain[32];
    U64 occ = pos.occ();
    int captured = (moveType(m) == MT_EP) ? PAWN
                 : (pos.board[to] == NO_PIECE ? NO_TYPE : pos.board[to] % 6);
    gain[0] = (captured == NO_TYPE) ? 0 : seeVal[captured];
    int attacker = pos.board[from] % 6;
    int stm = pos.stm;
    int d = 0;
    occ ^= 1ULL << from;
    if (moveType(m) == MT_EP) occ ^= 1ULL << (pos.stm == WHITE ? to - 8 : to + 8);
    while (true) {
        d++;
        gain[d] = seeVal[attacker] - gain[d - 1];
        stm ^= 1;
        U64 att = pos.attackersTo(to, occ) & occ & pos.byColor[stm];
        if (!att) break;
        // pick least valuable attacker
        int t;
        for (t = PAWN; t <= KING; t++)
            if (att & pos.byType[t]) break;
        attacker = t;
        occ ^= att & pos.byType[t] & -(att & pos.byType[t]);   // clear one bit
        if (d >= 30) break;
    }
    while (--d) {
        int v = -gain[d - 1] > gain[d] ? -gain[d - 1] : gain[d];
        gain[d - 1] = -v;
    }
    return gain[0];
}

// ------------------------------- perft --------------------------------------
static U64 perft(Position& pos, int depth) {
    MoveList list;
    pos.genMoves(list, false);
    U64 nodes = 0;
    for (int i = 0; i < list.n; i++) {
        pos.makeMove(list.m[i].move);
        if (!pos.lastMoveIllegal())
            nodes += (depth == 1) ? 1 : perft(pos, depth - 1);
        pos.unmakeMove(list.m[i].move);
    }
    return nodes;
}

// ------------------------------- evaluation ---------------------------------
// PeSTO tables (published on chessprogramming.org), index 0 = a8.
static const int mgValue[6] = { 82, 337, 365, 477, 1025, 0 };
static const int egValue[6] = { 94, 281, 297, 512, 936, 0 };
static const int gamephaseInc[6] = { 0, 1, 1, 2, 4, 0 };

static const int mgPawn[64] = {
      0,   0,   0,   0,   0,   0,  0,   0,
     98, 134,  61,  95,  68, 126, 34, -11,
     -6,   7,  26,  31,  65,  56, 25, -20,
    -14,  13,   6,  21,  23,  12, 17, -23,
    -27,  -2,  -5,  12,  17,   6, 10, -25,
    -26,  -4,  -4, -10,   3,   3, 33, -12,
    -35,  -1, -20, -23, -15,  24, 38, -22,
      0,   0,   0,   0,   0,   0,  0,   0,
};
static const int egPawn[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,
    178, 173, 158, 134, 147, 132, 165, 187,
     94, 100,  85,  67,  56,  53,  82,  84,
     32,  24,  13,   5,  -2,   4,  17,  17,
     13,   9,  -3,  -7,  -7,  -8,   3,  -1,
      4,   7,  -6,   1,   0,  -5,  -1,  -8,
     13,   8,   8,  10,  13,   0,   2,  -7,
      0,   0,   0,   0,   0,   0,   0,   0,
};
static const int mgKnight[64] = {
    -167, -89, -34, -49,  61, -97, -15, -107,
     -73, -41,  72,  36,  23,  62,   7,  -17,
     -47,  60,  37,  65,  84, 129,  73,   44,
      -9,  17,  19,  53,  37,  69,  18,   22,
     -13,   4,  16,  13,  28,  19,  21,   -8,
     -23,  -9,  12,  10,  19,  17,  25,  -16,
     -29, -53, -12,  -3,  -1,  18, -14,  -19,
    -105, -21, -58, -33, -17, -28, -19,  -23,
};
static const int egKnight[64] = {
    -58, -38, -13, -28, -31, -27, -63, -99,
    -25,  -8, -25,  -2,  -9, -25, -24, -52,
    -24, -20,  10,   9,  -1,  -9, -19, -41,
    -17,   3,  22,  22,  22,  11,   8, -18,
    -18,  -6,  16,  25,  16,  17,   4, -18,
    -23,  -3,  -1,  15,  10,  -3, -20, -22,
    -42, -20, -10,  -5,  -2, -20, -23, -44,
    -29, -51, -23, -15, -22, -18, -50, -64,
};
static const int mgBishop[64] = {
    -29,   4, -82, -37, -25, -42,   7,  -8,
    -26,  16, -18, -13,  30,  59,  18, -47,
    -16,  37,  43,  40,  35,  50,  37,  -2,
     -4,   5,  19,  50,  37,  37,   7,  -2,
     -6,  13,  13,  26,  34,  12,  10,   4,
      0,  15,  15,  15,  14,  27,  18,  10,
      4,  15,  16,   0,   7,  21,  33,   1,
    -33,  -3, -14, -21, -13, -12, -39, -21,
};
static const int egBishop[64] = {
    -14, -21, -11,  -8, -7,  -9, -17, -24,
     -8,  -4,   7, -12, -3, -13,  -4, -14,
      2,  -8,   0,  -1, -2,   6,   0,   4,
     -3,   9,  12,   9, 14,  10,   3,   2,
     -6,   3,  13,  19,  7,  10,  -3,  -9,
    -12,  -3,   8,  10, 13,   3,  -7, -15,
    -14, -18,  -7,  -1,  4,  -9, -15, -27,
    -23,  -9, -23,  -5, -9, -16,  -5, -17,
};
static const int mgRook[64] = {
     32,  42,  32,  51, 63,  9,  31,  43,
     27,  32,  58,  62, 80, 67,  26,  44,
     -5,  19,  26,  36, 17, 45,  61,  16,
    -24, -11,   7,  26, 24, 35,  -8, -20,
    -36, -26, -12,  -1,  9, -7,   6, -23,
    -45, -25, -16, -17,  3,  0,  -5, -33,
    -44, -16, -20,  -9, -1, 11,  -6, -71,
    -19, -13,   1,  17, 16,  7, -37, -26,
};
static const int egRook[64] = {
    13, 10, 18, 15, 12,  12,   8,   5,
    11, 13, 13, 11, -3,   3,   8,   3,
     7,  7,  7,  5,  4,  -3,  -5,  -3,
     4,  3, 13,  1,  2,   1,  -1,   2,
     3,  5,  8,  4, -5,  -6,  -8, -11,
    -4,  0, -5, -1, -7, -12,  -8, -16,
    -6, -6,  0,  2, -9,  -9, -11,  -3,
    -9,  2,  3, -1, -5, -13,   4, -20,
};
static const int mgQueen[64] = {
    -28,   0,  29,  12,  59,  44,  43,  45,
    -24, -39,  -5,   1, -16,  57,  28,  54,
    -13, -17,   7,   8,  29,  56,  47,  57,
    -27, -27, -16, -16,  -1,  17,  -2,   1,
     -9, -26,  -9, -10,  -2,  -4,   3,  -3,
    -14,   2, -11,  -2,  -5,   2,  14,   5,
    -35,  -8,  11,   2,   8,  15,  -3,   1,
     -1, -18,  -9,  10, -15, -25, -31, -50,
};
static const int egQueen[64] = {
     -9,  22,  22,  27,  27,  19,  10,  20,
    -17,  20,  32,  41,  58,  25,  30,   0,
    -20,   6,   9,  49,  47,  35,  19,   9,
      3,  22,  24,  45,  57,  40,  57,  36,
    -18,  28,  19,  47,  31,  34,  39,  23,
    -16, -27,  15,   6,   9,  17,  10,   5,
    -22, -23, -30, -16, -16, -23, -36, -32,
    -33, -28, -22, -43,  -5, -32, -20, -41,
};
static const int mgKing[64] = {
    -65,  23,  16, -15, -56, -34,   2,  13,
     29,  -1, -20,  -7,  -8,  -4, -38, -29,
     -9,  24,   2, -16, -20,   6,  22, -22,
    -17, -20, -12, -27, -30, -25, -14, -36,
    -49,  -1, -27, -39, -46, -44, -33, -51,
    -14, -14, -22, -46, -44, -30, -15, -27,
      1,   7,  -8, -64, -43, -16,   9,   8,
    -15,  36,  12, -54,   8, -28,  24,  14,
};
static const int egKing[64] = {
    -74, -35, -18, -18, -11,  15,   4, -17,
    -12,  17,  14,  17,  17,  38,  23,  11,
     10,  17,  23,  15,  20,  45,  44,  13,
     -8,  22,  24,  27,  26,  33,  26,   3,
    -18,  -4,  21,  24,  27,  23,   9, -11,
    -19,  -3,  11,  21,  23,  16,   7,  -9,
    -27, -11,   4,  13,  14,   4,  -5, -17,
    -53, -34, -21, -11, -28, -14, -24, -43,
};

static U64 fileMask[8], adjFileMask[8], passedMask[2][64];
static void initEvalMasks() {
    for (int f = 0; f < 8; f++) {
        fileMask[f] = FILE_A << f;
        adjFileMask[f] = 0;
        if (f > 0) adjFileMask[f] |= FILE_A << (f - 1);
        if (f < 7) adjFileMask[f] |= FILE_A << (f + 1);
    }
    for (int s = 0; s < 64; s++) {
        int f = s & 7, r = s >> 3;
        U64 span = fileMask[f] | adjFileMask[f];
        U64 ahead = 0, behind = 0;
        for (int rr = r + 1; rr < 8; rr++) ahead |= 0xFFULL << (8 * rr);
        for (int rr = 0; rr < r; rr++) behind |= 0xFFULL << (8 * rr);
        passedMask[WHITE][s] = span & ahead;
        passedMask[BLACK][s] = span & behind;
    }
}

static int mgTable[12][64], egTable[12][64];
static void initEval() {
    const int* mgT[6] = { mgPawn, mgKnight, mgBishop, mgRook, mgQueen, mgKing };
    const int* egT[6] = { egPawn, egKnight, egBishop, egRook, egQueen, egKing };
    for (int t = 0; t < 6; t++) {
        for (int s = 0; s < 64; s++) {
            // white: table row 0 = rank 8, so flip
            mgTable[t][s] = mgValue[t] + mgT[t][s ^ 56];
            egTable[t][s] = egValue[t] + egT[t][s ^ 56];
            mgTable[t + 6][s] = mgValue[t] + mgT[t][s];
            egTable[t + 6][s] = egValue[t] + egT[t][s];
        }
    }
}

static int evaluate(const Position& pos) {
    // insufficient material: bare kings, or a single minor each at most
    U64 majors = pos.byType[QUEEN] | pos.byType[ROOK] | pos.byType[PAWN];
    if (!majors) {
        int wm = popcnt(pos.byColor[WHITE] & (pos.byType[KNIGHT] | pos.byType[BISHOP]));
        int bm = popcnt(pos.byColor[BLACK] & (pos.byType[KNIGHT] | pos.byType[BISHOP]));
        if (wm <= 1 && bm <= 1) return 0;
    }
    int mg = 0, eg = 0, phase = 0;
    U64 occ = pos.occ();
    static const int passedMg[8] = { 0, 2, 6, 12, 25, 50, 90, 0 };
    static const int passedEg[8] = { 0, 8, 15, 30, 55, 100, 160, 0 };
    static const int kingAttW[6] = { 0, 2, 2, 3, 5, 0 };

    for (int c = 0; c < 2; c++) {
        int sign = (c == WHITE) ? 1 : -1;
        U64 ourPawns = pos.pieces(c, PAWN);
        U64 theirPawns = pos.pieces(c ^ 1, PAWN);
        U64 allPawns = ourPawns | theirPawns;
        U64 theirPawnAtt = (c == WHITE)
            ? (((theirPawns & ~FILE_A) >> 9) | ((theirPawns & ~FILE_H) >> 7))
            : (((theirPawns & ~FILE_A) << 7) | ((theirPawns & ~FILE_H) << 9));
        U64 mobArea = ~pos.byColor[c] & ~theirPawnAtt;
        U64 ourPawnAtt = (c == WHITE)
            ? (((ourPawns & ~FILE_A) << 7) | ((ourPawns & ~FILE_H) << 9))
            : (((ourPawns & ~FILE_A) >> 9) | ((ourPawns & ~FILE_H) >> 7));
        int theirKing = pos.kingSq(c ^ 1);
        U64 kingZone = kingAtt[theirKing] | (1ULL << theirKing);
        int attackers = 0, attackWeight = 0;
        int cmg = 0, ceg = 0;

        U64 bb = ourPawns;
        while (bb) {
            int s = poplsb(bb);
            int p = pos.board[s];
            cmg += mgTable[p][s]; ceg += egTable[p][s];
            int f = s & 7, r = s >> 3;
            int relRank = (c == WHITE) ? r : 7 - r;
            if (!(passedMask[c][s] & theirPawns)) {
                cmg += passedMg[relRank]; ceg += passedEg[relRank];
                int front = (c == WHITE) ? s + 8 : s - 8;
                int ff = front & 7, fr = front >> 3;
                int ourK = pos.kingSq(c);
                int dOur = max(abs((ourK & 7) - ff), abs((ourK >> 3) - fr));
                int dTheir = max(abs((theirKing & 7) - ff), abs((theirKing >> 3) - fr));
                ceg += (dTheir - dOur) * relRank * 2;
            }
            if (!(adjFileMask[f] & ourPawns)) { cmg -= 11; ceg -= 8; }
            if (passedMask[c][s] & fileMask[f] & ourPawns) { cmg -= 8; ceg -= 14; }
        }
        // pawn threats on their non-pawn pieces
        {
            int nt = popcnt(ourPawnAtt & (pos.byColor[c ^ 1] & ~theirPawns));
            cmg += 30 * nt; ceg += 25 * nt;
        }
        bb = pos.pieces(c, KNIGHT);
        while (bb) {
            int s = poplsb(bb);
            cmg += mgTable[c * 6 + KNIGHT][s]; ceg += egTable[c * 6 + KNIGHT][s];
            phase += 1;
            U64 att = knightAtt[s];
            int cnt = popcnt(att & mobArea);
            cmg += (cnt - 4) * 4; ceg += (cnt - 4) * 4;
            int relRank = (c == WHITE) ? (s >> 3) : 7 - (s >> 3);
            if (relRank >= 3 && relRank <= 5 && ((1ULL << s) & ourPawnAtt)
                && !(passedMask[c][s] & adjFileMask[s & 7] & theirPawns)) {
                cmg += 24; ceg += 12;   // outpost
            }
            if (att & kingZone) { attackers++; attackWeight += kingAttW[KNIGHT]; }
        }
        bb = pos.pieces(c, BISHOP);
        if (popcnt(bb) >= 2) { cmg += 28; ceg += 45; }
        while (bb) {
            int s = poplsb(bb);
            cmg += mgTable[c * 6 + BISHOP][s]; ceg += egTable[c * 6 + BISHOP][s];
            phase += 1;
            U64 att = bishopAtt(s, occ);
            int cnt = popcnt(att & mobArea);
            cmg += (cnt - 6) * 3; ceg += (cnt - 6) * 3;
            if (att & kingZone) { attackers++; attackWeight += kingAttW[BISHOP]; }
        }
        bb = pos.pieces(c, ROOK);
        while (bb) {
            int s = poplsb(bb);
            cmg += mgTable[c * 6 + ROOK][s]; ceg += egTable[c * 6 + ROOK][s];
            phase += 2;
            U64 att = rookAtt(s, occ);
            int cnt = popcnt(att & mobArea);
            cmg += (cnt - 7) * 2; ceg += (cnt - 7) * 4;
            int f = s & 7;
            if (!(fileMask[f] & allPawns)) { cmg += 25; ceg += 10; }
            else if (!(fileMask[f] & ourPawns)) { cmg += 12; ceg += 5; }
            if (att & kingZone) { attackers++; attackWeight += kingAttW[ROOK]; }
        }
        bb = pos.pieces(c, QUEEN);
        while (bb) {
            int s = poplsb(bb);
            cmg += mgTable[c * 6 + QUEEN][s]; ceg += egTable[c * 6 + QUEEN][s];
            phase += 4;
            U64 att = queenAtt(s, occ);
            int cnt = popcnt(att & mobArea);
            cmg += (cnt - 13); ceg += (cnt - 13) * 2;
            if (att & kingZone) { attackers++; attackWeight += kingAttW[QUEEN]; }
        }
        {
            int s = pos.kingSq(c);
            cmg += mgTable[c * 6 + KING][s]; ceg += egTable[c * 6 + KING][s];
            // pawn shield (mg only)
            int kf = s & 7;
            if (kf == 0) kf = 1;
            if (kf == 7) kf = 6;
            for (int f = kf - 1; f <= kf + 1; f++) {
                U64 fp = fileMask[f] & ourPawns;
                if (!fp) { cmg -= 9; continue; }
                int psq, pr;
                if (c == WHITE) { psq = lsb(fp); pr = psq >> 3; }
                else { psq = 63 - (int)_lzcnt_u64(fp); pr = 7 - (psq >> 3); }
                (void)psq;
                if (pr == 1) cmg += 8;
                else if (pr == 2) cmg += 3;
            }
        }
        if (!pos.pieces(c, QUEEN)) attackWeight /= 2;
        if (attackers >= 2) {
            int danger = attackWeight * attackWeight;
            if (danger > 300) danger = 300;
            cmg += danger;   // we are ATTACKING their king: bonus for us
        }
        mg += sign * cmg;
        eg += sign * ceg;
    }

    if (phase > 24) phase = 24;
    int score = (mg * phase + eg * (24 - phase)) / 24;
    score = (pos.stm == WHITE) ? score : -score;
    return score + 12;   // tempo
}

// ------------------------------- TT -----------------------------------------
enum { TT_NONE = 0, TT_EXACT = 1, TT_LOWER = 2, TT_UPPER = 3 };
struct TTEntry {
    U64 key;
    int16_t score;
    Move move;
    int8_t depth;
    uint8_t flag;
    int16_t eval;
};
static TTEntry* tt = nullptr;
static uint8_t ttAge = 0;
static size_t ttMask = 0;
static void ttResize(size_t mb) {
    if (tt) free(tt);
    size_t count = mb * 1024 * 1024 / sizeof(TTEntry);
    size_t pow2 = 1; while (pow2 * 2 <= count) pow2 *= 2;
    tt = (TTEntry*)calloc(pow2, sizeof(TTEntry));
    ttMask = pow2 - 1;
}
static void ttClear() { memset(tt, 0, (ttMask + 1) * sizeof(TTEntry)); }

// ------------------------------- search -------------------------------------
static const int MATE = 30000;
static const int MATE_BOUND = 29000;
static const int MAX_PLY = 128;

struct SearchInfo {
    U64 nodes = 0;
    int seldepth = 0;
    bool hardLimitOn = false;
    chrono::steady_clock::time_point start;
    long long hardMs = 0, softMs = 0;
};
static SearchInfo si;
static atomic<bool> stopFlag(false);
static atomic<bool> quitFlag(false);
static atomic<bool> searching(false);

static Move killers[MAX_PLY][2];
static int historyTab[2][64][64];
static int16_t contHist[12][64][12][64];   // [prevPiece][prevTo][piece][to]
static int16_t capHist[12][64][6];         // [attacker][to][victim type]
static uint8_t pieceStack[MAX_PLY + 2];
static Move counterMove[2][64][64];
static inline void gravity(int& h, int bonus) { h += bonus - h * (bonus > 0 ? bonus : -bonus) / 16384; }
static inline void gravity16(int16_t& h, int bonus) {
    int v = h + bonus - (int)h * (bonus > 0 ? bonus : -bonus) / 16384;
    h = (int16_t)v;
}
static Move pvTable[MAX_PLY][MAX_PLY];
static int pvLen[MAX_PLY];
static int lmrTable[64][64];
static int evalStack[MAX_PLY];
static Move moveStack[MAX_PLY + 2];
static Move excludedAt[MAX_PLY + 2];
static Move rootBestMove = 0;

// tunable parameters (adjustable via hidden UCI options for A/B testing)
static int P_LMRDIV = 225;      // lmr divisor x100
static int P_RFP = 90;          // reverse futility margin per depth
static int P_FUTB = 120, P_FUTS = 110;   // futility base/scale
static int P_ASPD = 25;         // aspiration delta
static int P_SOFTDIV = 28;      // soft time = t / SOFTDIV + inc * INCPCT/100
static int P_INCPCT = 75;
static int P_HARDNUM = 4;       // hard = t / HARDNUM cap

static void initLmr() {
    for (int d = 1; d < 64; d++)
        for (int m = 1; m < 64; m++)
            lmrTable[d][m] = (int)(0.5 + log((double)d) * log((double)m) * 100.0 / P_LMRDIV);
}

static long long elapsedMs() {
    return chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now() - si.start).count();
}

static inline void checkTime() {
    if ((si.nodes & 2047) == 0) {
        if (si.hardLimitOn && elapsedMs() >= si.hardMs) stopFlag = true;
    }
}

static const int mvvValue[7] = { 100, 320, 330, 500, 900, 10000, 0 };

static void scoreMoves(const Position& pos, MoveList& list, Move ttMove, int ply, Move prev) {
    Move counter = prev ? counterMove[pos.stm][moveFrom(prev)][moveTo(prev)] : 0;
    const int16_t* ch1 = (ply >= 1 && moveStack[ply - 1])
        ? &contHist[pieceStack[ply - 1]][moveTo(moveStack[ply - 1])][0][0] : nullptr;
    const int16_t* ch2 = (ply >= 2 && moveStack[ply - 2])
        ? &contHist[pieceStack[ply - 2]][moveTo(moveStack[ply - 2])][0][0] : nullptr;
    for (int i = 0; i < list.n; i++) {
        Move m = list.m[i].move;
        if (m == ttMove) { list.m[i].score = 1 << 30; continue; }
        int type = moveType(m);
        int victim = (type == MT_EP) ? PAWN
                   : (pos.board[moveTo(m)] == NO_PIECE ? NO_TYPE : pos.board[moveTo(m)] % 6);
        if (victim != NO_TYPE) {
            int base = mvvValue[victim] * 100 - pos.board[moveFrom(m)] % 6
                     + capHist[pos.board[moveFrom(m)]][moveTo(m)][victim] / 8;
            if (type == MT_PROMO) base += mvvValue[movePromo(m)];
            list.m[i].score = (see(pos, m) >= 0 ? 100000000 : -300000) + base;
        } else if (type == MT_PROMO) {
            list.m[i].score = 90000000 + mvvValue[movePromo(m)];
        } else if (m == killers[ply][0]) {
            list.m[i].score = 80000000;
        } else if (m == killers[ply][1]) {
            list.m[i].score = 79000000;
        } else if (m == counter) {
            list.m[i].score = 78000000;
        } else {
            int sc = historyTab[pos.stm][moveFrom(m)][moveTo(m)];
            int pc = pos.board[moveFrom(m)];
            if (ch1) sc += ch1[pc * 64 + moveTo(m)];
            if (ch2) sc += ch2[pc * 64 + moveTo(m)];
            list.m[i].score = sc;
        }
    }
}

static Move pickMove(MoveList& list, int i) {
    int best = i;
    for (int j = i + 1; j < list.n; j++)
        if (list.m[j].score > list.m[best].score) best = j;
    swap(list.m[i], list.m[best]);
    return list.m[i].move;
}

static int qsearch(Position& pos, int alpha, int beta, int ply) {
    si.nodes++;
    checkTime();
    if (stopFlag) return 0;
    if (ply > si.seldepth) si.seldepth = ply;
    if (ply >= MAX_PLY - 1) return evaluate(pos);

    // TT probe (depth 0)
    TTEntry& e = tt[pos.key & ttMask];
    Move ttMove = 0;
    if (e.key == pos.key) {
        ttMove = e.move;
        if (beta - alpha == 1) {
            int s = e.score;
            if (s > MATE_BOUND) s -= ply;
            else if (s < -MATE_BOUND) s += ply;
            if ((e.flag & 3) == TT_EXACT ||
                ((e.flag & 3) == TT_LOWER && s >= beta) ||
                ((e.flag & 3) == TT_UPPER && s <= alpha))
                return s;
        }
    }

    bool inCheck = pos.inCheck();
    int best;
    int standPat = -32001;
    if (inCheck) {
        best = -MATE + ply;   // if no evasions found → mate
    } else {
        standPat = (e.key == pos.key && e.eval != -32001) ? e.eval : evaluate(pos);
        best = standPat;
        if (best >= beta) return best;
        if (best > alpha) alpha = best;
    }

    MoveList list;
    pos.genMoves(list, !inCheck);   // evasions: all moves; else captures only
    scoreMoves(pos, list, ttMove, ply, 0);

    int oldAlpha = alpha;
    Move bestMove = 0;
    for (int i = 0; i < list.n; i++) {
        Move m = pickMove(list, i);
        if (!inCheck && list.m[i].score < 0 && best > -MATE_BOUND)
            break;   // losing captures: prune
        // delta pruning
        if (!inCheck && moveType(m) != MT_PROMO && best > -MATE_BOUND) {
            int victim = (moveType(m) == MT_EP) ? PAWN : pos.board[moveTo(m)] % 6;
            if (best + seeVal[victim] + 150 < alpha) continue;
        }
        pos.makeMove(m);
        if (pos.lastMoveIllegal()) { pos.unmakeMove(m); continue; }
        moveStack[ply] = m;
        pieceStack[ply] = pos.board[moveTo(m)];
        int score = -qsearch(pos, -beta, -alpha, ply + 1);
        pos.unmakeMove(m);
        if (stopFlag) return 0;
        if (score > best) {
            best = score;
            bestMove = m;
            if (score > alpha) {
                alpha = score;
                if (score >= beta) break;
            }
        }
    }
    int flag = (best >= beta) ? TT_LOWER : (alpha > oldAlpha ? TT_EXACT : TT_UPPER);
    int s2 = best;
    if (s2 > MATE_BOUND) s2 += ply;
    else if (s2 < -MATE_BOUND) s2 -= ply;
    if (e.key != pos.key || e.depth <= 0 || (e.flag >> 2) != (ttAge & 63)) {
        e.key = pos.key; e.score = (int16_t)s2; e.move = bestMove;
        e.depth = 0; e.flag = (uint8_t)(flag | ((ttAge & 63) << 2));
        e.eval = (int16_t)standPat;
    }
    return best;
}

static bool isRepetitionOrFifty(const Position& pos) {
    if (pos.fifty >= 100) return true;
    int cnt = 0;
    int end = pos.histPly - pos.fifty;
    for (int i = pos.histPly - 2; i >= end && i >= 0; i -= 2) {
        if (pos.keyHist[i] == pos.key) return true;
    }
    (void)cnt;
    return false;
}

static int search(Position& pos, int depth, int ply, int alpha, int beta, bool doNull) {
    bool isPV = (beta - alpha) > 1;
    bool isRoot = (ply == 0);

    pvLen[ply] = ply;

    if (!isRoot) {
        if (isRepetitionOrFifty(pos)) return 0;
        if (ply >= MAX_PLY - 1) return evaluate(pos);
        // mate distance pruning
        int mateAlpha = alpha > -MATE + ply ? alpha : -MATE + ply;
        int mateBeta = beta < MATE - ply - 1 ? beta : MATE - ply - 1;
        if (mateAlpha >= mateBeta) return mateAlpha;
    }

    bool inCheck = pos.inCheck();
    if (inCheck) depth++;   // check extension
    if (depth <= 0) return qsearch(pos, alpha, beta, ply);

    si.nodes++;
    checkTime();
    if (stopFlag) return 0;

    Move excluded = excludedAt[ply];

    // TT probe
    TTEntry& e = tt[pos.key & ttMask];
    Move ttMove = 0;
    if (e.key == pos.key && !excluded) {
        ttMove = e.move;
        if (!isPV && e.depth >= depth) {
            int s = e.score;
            if (s > MATE_BOUND) s -= ply;
            else if (s < -MATE_BOUND) s += ply;
            if ((e.flag & 3) == TT_EXACT ||
                ((e.flag & 3) == TT_LOWER && s >= beta) ||
                ((e.flag & 3) == TT_UPPER && s <= alpha))
                return s;
        }
    }

    // internal iterative reduction
    if (depth >= 4 && !ttMove && !excluded) depth--;

    bool ttHit = (e.key == pos.key);
    int staticEval = (ttHit && e.eval != -32001) ? e.eval : evaluate(pos);
    evalStack[ply] = staticEval;
    // refine eval with TT score when its bound applies
    int refEval = staticEval;
    if (ttHit && !excluded && e.score > -MATE_BOUND && e.score < MATE_BOUND) {
        if (((e.flag & 3) == TT_LOWER && e.score > refEval) ||
            ((e.flag & 3) == TT_UPPER && e.score < refEval) ||
            (e.flag & 3) == TT_EXACT)
            refEval = e.score;
    }
    bool improving = !inCheck && ply >= 2 && staticEval > evalStack[ply - 2];

    // razoring
    if (!isPV && !inCheck && depth <= 2 && refEval + 250 * depth <= alpha) {
        int v = qsearch(pos, alpha, alpha + 1, ply);
        if (v <= alpha) return v;
    }

    // reverse futility pruning
    if (!isPV && !inCheck && depth <= 6
        && refEval - P_RFP * depth + (improving ? 50 : 0) >= beta
        && abs(beta) < MATE_BOUND)
        return refEval;

    // null move pruning
    if (!isPV && !inCheck && doNull && !excluded && depth >= 3 && refEval >= beta
        && (pos.byColor[pos.stm] & ~(pos.byType[PAWN] | pos.byType[KING]))) {
        int R = 3 + depth / 5 + ((refEval - beta) > 300 ? 1 : 0);
        moveStack[ply] = 0;
        pos.makeNull();
        int score = -search(pos, depth - 1 - R, ply + 1, -beta, -beta + 1, false);
        pos.unmakeNull();
        if (stopFlag) return 0;
        if (score >= beta) return (score > MATE_BOUND) ? beta : score;
    }

    Move prevMove = ply > 0 ? moveStack[ply - 1] : 0;

    // singular extension / multicut
    int singularExt = 0;
    if (!isRoot && !excluded && depth >= 8 && ttMove && e.key == pos.key
        && e.depth >= depth - 3 && (e.flag & 3) != TT_UPPER
        && e.score > -MATE_BOUND && e.score < MATE_BOUND) {
        int sBeta = e.score - 2 * depth;
        excludedAt[ply] = ttMove;
        int sScore = search(pos, (depth - 1) / 2, ply, sBeta - 1, sBeta, false);
        excludedAt[ply] = 0;
        if (stopFlag) return 0;
        if (sScore < sBeta) singularExt = 1;
        else if (sBeta >= beta) return sBeta;   // multicut
    }

    MoveList list;
    pos.genMoves(list, false);
    scoreMoves(pos, list, ttMove, ply, prevMove);

    int best = -MATE;
    Move bestMove = 0;
    int legal = 0;
    int oldAlpha = alpha;
    Move quietsTried[64];
    int nQuiets = 0;
    Move capsTried[48];
    int nCaps = 0;
    int lmpLimit = (3 + depth * depth) / (improving ? 1 : 2);

    for (int i = 0; i < list.n; i++) {
        Move m = pickMove(list, i);
        if (m == excluded) continue;
        bool isCapture = pos.board[moveTo(m)] != NO_PIECE || moveType(m) == MT_EP;
        bool isPromo = moveType(m) == MT_PROMO;
        bool quiet = !isCapture && !isPromo;

        if (!isPV && !inCheck && quiet && legal >= 1 && best > -MATE_BOUND) {
            // late move pruning
            if (depth <= 4 && legal > lmpLimit) continue;
            // futility pruning
            if (depth <= 6 && staticEval + P_FUTB + P_FUTS * depth <= alpha) continue;
        }
        // SEE pruning
        if (legal >= 1 && best > -MATE_BOUND && depth <= 8) {
            if (isCapture) {
                if (list.m[i].score < 0 && see(pos, m) < -100 * depth) continue;
            } else if (!isPV && !inCheck && see(pos, m) < -60 * depth) continue;
        }

        pos.makeMove(m);
        __builtin_prefetch(&tt[pos.key & ttMask]);
        if (pos.lastMoveIllegal()) { pos.unmakeMove(m); continue; }
        legal++;
        moveStack[ply] = m;
        pieceStack[ply] = pos.board[moveTo(m)];
        if (quiet && nQuiets < 64) quietsTried[nQuiets++] = m;
        else if (isCapture && nCaps < 48) capsTried[nCaps++] = m;
        int ext = (m == ttMove) ? singularExt : 0;

        int score;
        if (legal == 1) {
            score = -search(pos, depth - 1 + ext, ply + 1, -beta, -alpha, true);
        } else {
            int r = 0;
            if (quiet && depth >= 3 && legal > 3) {
                r = lmrTable[depth < 64 ? depth : 63][legal < 64 ? legal : 63];
                if (isPV && r > 0) r--;
                if (!improving) r++;
                int hadj = list.m[i].score / 6000;
                if (hadj > 2) hadj = 2;
                if (hadj < -2) hadj = -2;
                r -= hadj;
                if (r < 0) r = 0;
                if (r > depth - 2) r = depth - 2;
            }
            score = -search(pos, depth - 1 - r, ply + 1, -alpha - 1, -alpha, true);
            if (score > alpha && r > 0)
                score = -search(pos, depth - 1, ply + 1, -alpha - 1, -alpha, true);
            if (score > alpha && score < beta)
                score = -search(pos, depth - 1, ply + 1, -beta, -alpha, true);
        }
        pos.unmakeMove(m);
        if (stopFlag) return 0;

        if (score > best) {
            best = score;
            bestMove = m;
            if (score > alpha) {
                alpha = score;
                if (isRoot) rootBestMove = m;
                pvTable[ply][ply] = m;
                for (int j = ply + 1; j < pvLen[ply + 1]; j++)
                    pvTable[ply][j] = pvTable[ply + 1][j];
                pvLen[ply] = pvLen[ply + 1];
                if (score >= beta) {
                    {
                        int bonus = depth * depth < 1200 ? depth * depth + 30 : 1200;
                        for (int q = 0; q < nCaps; q++) {
                            Move cm = capsTried[q];
                            if (cm == m) continue;
                            int vict = (moveType(cm) == MT_EP) ? PAWN : pos.board[moveTo(cm)] % 6;
                            gravity16(capHist[pos.board[moveFrom(cm)]][moveTo(cm)][vict], -bonus);
                        }
                        if (isCapture) {
                            int vict = (moveType(m) == MT_EP) ? PAWN : pos.board[moveTo(m)] % 6;
                            gravity16(capHist[pos.board[moveFrom(m)]][moveTo(m)][vict], bonus);
                        }
                    }
                    if (quiet) {
                        if (killers[ply][0] != m) {
                            killers[ply][1] = killers[ply][0];
                            killers[ply][0] = m;
                        }
                        if (prevMove)
                            counterMove[pos.stm][moveFrom(prevMove)][moveTo(prevMove)] = m;
                        int bonus = depth * depth < 1200 ? depth * depth + 30 : 1200;
                        int16_t* uch1 = (ply >= 1 && moveStack[ply - 1])
                            ? &contHist[pieceStack[ply - 1]][moveTo(moveStack[ply - 1])][0][0] : nullptr;
                        int16_t* uch2 = (ply >= 2 && moveStack[ply - 2])
                            ? &contHist[pieceStack[ply - 2]][moveTo(moveStack[ply - 2])][0][0] : nullptr;
                        for (int q = 0; q < nQuiets; q++) {
                            Move qm = quietsTried[q];
                            int b = (qm == m) ? bonus : -bonus;
                            gravity(historyTab[pos.stm][moveFrom(qm)][moveTo(qm)], b);
                            int qpc = pos.board[moveFrom(qm)];
                            if (uch1) gravity16(uch1[qpc * 64 + moveTo(qm)], b);
                            if (uch2) gravity16(uch2[qpc * 64 + moveTo(qm)], b);
                        }
                    }
                    break;
                }
            }
        }
    }

    if (!legal) return excluded ? alpha : (inCheck ? -MATE + ply : 0);

    // store TT
    if (!excluded) {
        int flag = (best >= beta) ? TT_LOWER : (alpha > oldAlpha ? TT_EXACT : TT_UPPER);
        int s = best;
        if (s > MATE_BOUND) s += ply;
        else if (s < -MATE_BOUND) s -= ply;
        if (e.key != pos.key || depth >= e.depth || flag == TT_EXACT
            || (e.flag >> 2) != (ttAge & 63)) {
            e.key = pos.key; e.score = (int16_t)s; e.move = bestMove;
            e.depth = (int8_t)depth; e.flag = (uint8_t)(flag | ((ttAge & 63) << 2));
            e.eval = (int16_t)(inCheck ? -32001 : staticEval);
        }
    }
    return best;
}

static mutex printMx;
static void sendLine(const string& s) {
    lock_guard<mutex> lk(printMx);
    fputs(s.c_str(), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

struct GoParams {
    long long wtime = -1, btime = -1, winc = 0, binc = 0, movetime = -1;
    int depth = -1, movestogo = -1;
    bool infinite = false;
};

static Move lastBestMove = 0;

static void iterativeDeepening(Position& pos, const GoParams& gp) {
    si.nodes = 0;
    si.seldepth = 0;
    si.start = chrono::steady_clock::now();
    si.hardLimitOn = false;
    ttAge++;
    memset(killers, 0, sizeof(killers));

    long long myTime = pos.stm == WHITE ? gp.wtime : gp.btime;
    long long myInc = pos.stm == WHITE ? gp.winc : gp.binc;
    int maxDepth = gp.depth > 0 ? gp.depth : MAX_PLY - 8;

    if (gp.movetime > 0) {
        si.hardLimitOn = true;
        si.hardMs = gp.movetime > 30 ? gp.movetime - 10 : gp.movetime;
        si.softMs = si.hardMs;
    } else if (myTime > 0) {
        si.hardLimitOn = true;
        long long overhead = 25;
        long long t = myTime - overhead;
        if (t < 1) t = 1;
        int mtg = gp.movestogo > 0 ? gp.movestogo : P_SOFTDIV;
        long long soft = t / mtg + myInc * P_INCPCT / 100;
        long long hard = soft * 4;
        if (hard > t / P_HARDNUM) hard = t / P_HARDNUM;
        if (soft > hard) soft = hard;
        if (hard < 2) hard = 2;
        if (soft < 1) soft = 1;
        si.softMs = soft;
        si.hardMs = hard;
    }

    Move bestMove = 0;
    int lastScore = 0;
    int stability = 0;
    rootBestMove = 0;

    for (int depth = 1; depth <= maxDepth; depth++) {
        int alpha = -MATE, beta = MATE;
        int delta = P_ASPD;
        if (depth >= 5) { alpha = lastScore - delta; beta = lastScore + delta; }
        int score;
        while (true) {
            score = search(pos, depth, 0, alpha, beta, true);
            if (stopFlag) break;
            if (score <= alpha) { alpha -= delta; delta *= 3; if (alpha < -MATE) alpha = -MATE; }
            else if (score >= beta) { beta += delta; delta *= 3; if (beta > MATE) beta = MATE; }
            else break;
        }
        if (rootBestMove) {
            if (rootBestMove == bestMove) { if (stability < 8) stability++; }
            else stability = 0;
            bestMove = rootBestMove;
        }
        if (stopFlag && depth > 1) break;

        bool scoreDrop = depth >= 6 && score < lastScore - 25;
        lastScore = score;

        long long ms = elapsedMs();
        {
            ostringstream out;
            out << "info depth " << depth << " seldepth " << si.seldepth;
            if (abs(score) > MATE_BOUND) {
                int mate = (MATE - abs(score) + 1) / 2;
                out << " score mate " << (score > 0 ? mate : -mate);
            } else {
                out << " score cp " << score;
            }
            out << " nodes " << si.nodes
                << " nps " << (ms > 0 ? si.nodes * 1000 / ms : si.nodes)
                << " time " << ms << " pv";
            for (int i = 0; i < pvLen[0]; i++) out << " " << moveUci(pvTable[0][i]);
            sendLine(out.str());
        }
        if (stopFlag) break;
        if (si.hardLimitOn && gp.movetime <= 0) {
            static const int stabPct[9] = { 160, 130, 115, 100, 95, 85, 80, 80, 80 };
            long long adjSoft = si.softMs * stabPct[stability] / 100;
            if (scoreDrop) adjSoft = adjSoft * 150 / 100;
            if (ms > adjSoft) break;
        }
        if (si.hardLimitOn && gp.movetime > 0 && ms >= si.softMs) break;
        if (abs(score) > MATE_BOUND && depth >= 12) break;
    }

    if (!bestMove) {
        // emergency: any legal move
        MoveList list;
        pos.genMoves(list, false);
        for (int i = 0; i < list.n; i++) {
            pos.makeMove(list.m[i].move);
            bool bad = pos.lastMoveIllegal();
            pos.unmakeMove(list.m[i].move);
            if (!bad) { bestMove = list.m[i].move; break; }
        }
    }
    lastBestMove = bestMove;

    if (gp.infinite) {
        while (!stopFlag && !quitFlag) this_thread::sleep_for(chrono::milliseconds(1));
    }
    sendLine("bestmove " + moveUci(bestMove));
}

// ------------------------------- UCI ----------------------------------------
static Position rootPos;

static deque<string> cmdQ;
static mutex qMx;
static condition_variable qCv;

static void readerThread() {
    string line;
    while (getline(cin, line)) {
        if (line == "stop") { stopFlag = true; continue; }
        if (line == "quit") {
            stopFlag = true; quitFlag = true;
            lock_guard<mutex> lk(qMx);
            cmdQ.push_back(line);
            qCv.notify_one();
            break;
        }
        if (line == "isready" && searching) { sendLine("readyok"); continue; }
        {
            lock_guard<mutex> lk(qMx);
            cmdQ.push_back(line);
        }
        qCv.notify_one();
    }
    // stdin closed: behave like quit
    stopFlag = true; quitFlag = true;
    {
        lock_guard<mutex> lk(qMx);
        cmdQ.push_back("quit");
    }
    qCv.notify_one();
}

static void handlePosition(istringstream& ss) {
    string tok;
    ss >> tok;
    if (tok == "startpos") {
        rootPos.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        ss >> tok;   // maybe "moves"
    } else if (tok == "fen") {
        string fen, part;
        while (ss >> part) {
            if (part == "moves") { tok = part; break; }
            fen += part + " ";
        }
        rootPos.setFen(fen);
    }
    if (tok == "moves") {
        string ms;
        while (ss >> ms) {
            MoveList list;
            rootPos.genMoves(list, false);
            bool found = false;
            for (int i = 0; i < list.n; i++) {
                if (moveUci(list.m[i].move) == ms) {
                    rootPos.makeMove(list.m[i].move);
                    if (rootPos.lastMoveIllegal()) { rootPos.unmakeMove(list.m[i].move); continue; }
                    found = true;
                    break;
                }
            }
            if (!found) break;
        }
    }
}

static void runPerftSuite(const string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) { sendLine("info string cannot open " + path); return; }
    char buf[1024];
    int lineNo = 0, failures = 0;
    U64 totalNodes = 0;
    auto t0 = chrono::steady_clock::now();
    while (fgets(buf, sizeof(buf), f)) {
        lineNo++;
        string line(buf);
        size_t d1 = line.find(";D");
        if (d1 == string::npos) continue;
        string fen = line.substr(0, d1);
        Position p;
        p.setFen(fen);
        size_t pos2 = d1;
        while (pos2 != string::npos) {
            size_t next = line.find(";D", pos2 + 1);
            string tokstr = line.substr(pos2 + 2, (next == string::npos ? line.size() : next) - pos2 - 2);
            int d; long long expect;
            if (sscanf(tokstr.c_str(), "%d %lld", &d, &expect) == 2) {
                if (expect <= 200000000LL) {
                    U64 got = perft(p, d);
                    totalNodes += got;
                    if ((long long)got != expect) {
                        failures++;
                        printf("FAIL line %d D%d expect %lld got %llu fen %s\n",
                               lineNo, d, expect, (unsigned long long)got, fen.c_str());
                        fflush(stdout);
                    }
                }
            }
            pos2 = next;
        }
    }
    fclose(f);
    auto ms = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - t0).count();
    printf("perft suite done: %d lines, failures %d, nodes %llu, time %lld ms, nps %lld\n",
           lineNo, failures, (unsigned long long)totalNodes, (long long)ms,
           ms > 0 ? (long long)(totalNodes * 1000 / ms) : 0);
    fflush(stdout);
}

static void uciLoop() {
    rootPos.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    while (true) {
        string line;
        {
            unique_lock<mutex> lk(qMx);
            qCv.wait(lk, [] { return !cmdQ.empty(); });
            line = cmdQ.front();
            cmdQ.pop_front();
        }
        istringstream ss(line);
        string cmd;
        ss >> cmd;
        if (cmd == "uci") {
            sendLine("id name Fable 5 chess 24hrs");
            sendLine("id author Fable 5");
            sendLine("option name Hash type spin default 64 min 1 max 4096");
            sendLine("option name Threads type spin default 1 min 1 max 1");
            sendLine("uciok");
        } else if (cmd == "isready") {
            sendLine("readyok");
        } else if (cmd == "ucinewgame") {
            ttClear();
            memset(historyTab, 0, sizeof(historyTab));
            memset(killers, 0, sizeof(killers));
            memset(counterMove, 0, sizeof(counterMove));
            memset(contHist, 0, sizeof(contHist));
            memset(capHist, 0, sizeof(capHist));
            memset(capHist, 0, sizeof(capHist));
        } else if (cmd == "setoption") {
            string tok, name, value;
            ss >> tok;   // "name"
            while (ss >> tok && tok != "value") { if (!name.empty()) name += " "; name += tok; }
            while (ss >> tok) { if (!value.empty()) value += " "; value += tok; }
            if (name == "Hash") {
                long mb = atol(value.c_str());
                if (mb < 1) mb = 1;
                if (mb > 4096) mb = 4096;
                ttResize((size_t)mb);
            }
            // hidden tuning options
            else if (name == "LMRdiv") { P_LMRDIV = atoi(value.c_str()); initLmr(); }
            else if (name == "RFP") P_RFP = atoi(value.c_str());
            else if (name == "FutB") P_FUTB = atoi(value.c_str());
            else if (name == "FutS") P_FUTS = atoi(value.c_str());
            else if (name == "AspD") P_ASPD = atoi(value.c_str());
            else if (name == "SoftDiv") P_SOFTDIV = atoi(value.c_str());
            else if (name == "IncPct") P_INCPCT = atoi(value.c_str());
            else if (name == "HardNum") P_HARDNUM = atoi(value.c_str());
        } else if (cmd == "position") {
            handlePosition(ss);
        } else if (cmd == "go") {
            GoParams gp;
            string tok;
            bool isPerft = false;
            int perftDepth = 0;
            while (ss >> tok) {
                if (tok == "wtime") ss >> gp.wtime;
                else if (tok == "btime") ss >> gp.btime;
                else if (tok == "winc") ss >> gp.winc;
                else if (tok == "binc") ss >> gp.binc;
                else if (tok == "movetime") ss >> gp.movetime;
                else if (tok == "depth") ss >> gp.depth;
                else if (tok == "movestogo") ss >> gp.movestogo;
                else if (tok == "infinite") gp.infinite = true;
                else if (tok == "perft") { isPerft = true; ss >> perftDepth; }
            }
            if (isPerft) {
                auto t0 = chrono::steady_clock::now();
                U64 n = perft(rootPos, perftDepth);
                auto ms = chrono::duration_cast<chrono::milliseconds>(
                    chrono::steady_clock::now() - t0).count();
                printf("perft %d = %llu  (%lld ms, %lld nps)\n", perftDepth,
                       (unsigned long long)n, (long long)ms,
                       ms > 0 ? (long long)(n * 1000 / ms) : 0);
                fflush(stdout);
                continue;
            }
            if (gp.wtime < 0 && gp.btime < 0 && gp.movetime < 0 && gp.depth < 0 && !gp.infinite)
                gp.movetime = 2000;
            stopFlag = false;
            searching = true;
            iterativeDeepening(rootPos, gp);
            searching = false;
        } else if (cmd == "bench") {
            static const char* benchFens[] = {
                "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                "r1bq1rk1/pp2ppbp/2np1np1/8/3NP3/2N1BP2/PPPQ2PP/R3KB1R w KQ - 5 9",
                "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
                "r2q1rk1/ppp2ppp/3bbn2/3p4/8/1P1P2P1/PBPN1PBP/R2Q1RK1 b - - 3 10",
                "2kr3r/pp3p2/2n1b2p/2pp2p1/6P1/2PP1N1P/PP2PP2/R1B1K2R w KQ - 0 14",
                "8/8/1p1k4/p1p2p2/P1P2P1p/1P1K3P/8/8 w - - 0 40",
                "6k1/5ppp/1q6/8/8/2N5/5PPP/3Q2K1 w - - 0 1",
            };
            U64 total = 0;
            auto t0 = chrono::steady_clock::now();
            stopFlag = false;
            for (auto f : benchFens) {
                Position p;
                p.setFen(f);
                si.nodes = 0; si.seldepth = 0;
                si.start = chrono::steady_clock::now();
                si.hardLimitOn = false;
                rootBestMove = 0;
                for (int d = 1; d <= 12; d++) search(p, d, 0, -MATE, MATE, true);
                total += si.nodes;
            }
            auto ms = chrono::duration_cast<chrono::milliseconds>(
                chrono::steady_clock::now() - t0).count();
            printf("bench: nodes %llu time %lld ms nps %lld\n", (unsigned long long)total,
                   (long long)ms, ms > 0 ? (long long)(total * 1000 / ms) : 0);
            fflush(stdout);
        } else if (cmd == "eval") {
            printf("static eval (stm pov): %d\n", evaluate(rootPos));
            fflush(stdout);
        } else if (cmd == "symtest") {
            static const char* symFens[] = {
                "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                "r1bq1rk1/pp2ppbp/2np1np1/8/3NP3/2N1BP2/PPPQ2PP/R3KB1R w KQ - 5 9",
                "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
                "2kr3r/pp3p2/2n1b2p/2pp2p1/6P1/2PP1N1P/PP2PP2/R1B1K2R w KQ - 0 14",
                "8/8/1p1k4/p1p2p2/P1P2P1p/1P1K3P/8/8 w - - 0 40",
                "6k1/5ppp/1q6/8/8/2N5/5PPP/3Q2K1 w - - 0 1",
                "4k3/8/8/8/8/8/PPP5/4K3 w - - 0 1",
            };
            int bad = 0;
            for (auto f : symFens) {
                Position a, b;
                a.setFen(f);
                b.setFen(f);
                // mirror b: flip ranks, swap colors
                memset(b.byType, 0, sizeof(b.byType));
                memset(b.byColor, 0, sizeof(b.byColor));
                for (int i = 0; i < 64; i++) b.board[i] = NO_PIECE;
                for (int s = 0; s < 64; s++)
                    if (a.board[s] != NO_PIECE)
                        b.put((a.board[s] + 6) % 12, s ^ 56);
                b.stm = a.stm ^ 1;
                b.castling = ((a.castling & 3) << 2) | ((a.castling >> 2) & 3);
                b.ep = a.ep >= 0 ? (a.ep ^ 56) : -1;
                int ea = evaluate(a), eb = evaluate(b);
                if (ea != eb) { bad++; printf("ASYM %d vs %d: %s\n", ea, eb, f); }
            }
            printf("symtest: %d asymmetries\n", bad);
            fflush(stdout);
        } else if (cmd == "perfttest") {
            string path;
            ss >> path;
            runPerftSuite(path);
        } else if (cmd == "quit") {
            break;
        }
    }
}

int main(int argc, char** argv) {
    initAttacks();
    initCastleMask();
    initZobrist();
    initEvalMasks();
    initEval();
    initLmr();
    ttResize(64);
    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc > 2 && string(argv[1]) == "perfttest") {
        runPerftSuite(argv[2]);
        return 0;
    }

    thread reader(readerThread);
    uciLoop();
    reader.detach();
    return 0;
}




