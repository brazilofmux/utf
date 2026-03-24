/*
 * nfc.c — Unicode NFC normalization for UTF-8 strings.
 *
 * Ported from TinyMUX mux/lib/utf8_normalize.cpp (C++) to pure C.
 *
 * Algorithm (UAX #15):
 *   1. Decompose: Expand each code point to NFD (canonical decomposition).
 *      Hangul syllables decomposed algorithmically.
 *   2. Reorder: Sort combining marks by Canonical Combining Class (stable).
 *   3. Compose: Combine starter + combining mark pairs back into precomposed
 *      forms where possible.  Hangul composed algorithmically.
 *
 * Unicode 16.0.
 */

#include "utf/nfc.h"
#include "utf/utf_tables.h"
#include <string.h>

/* Hangul constants (Unicode 3.0+ algorithmic composition/decomposition). */
#define HANGUL_SBASE  0xAC00
#define HANGUL_LBASE  0x1100
#define HANGUL_VBASE  0x1161
#define HANGUL_TBASE  0x11A7
#define HANGUL_LCOUNT 19
#define HANGUL_VCOUNT 21
#define HANGUL_TCOUNT 28
#define HANGUL_NCOUNT (HANGUL_VCOUNT * HANGUL_TCOUNT)
#define HANGUL_SCOUNT (HANGUL_LCOUNT * HANGUL_NCOUNT)

#define UNI_EOF       ((uint32_t)-1)

#define UTF8_CONTINUE 5

/* Maximum code points per normalization segment.
 * A segment is one combining character sequence (starter + combiners).
 * 128 is generous — real sequences rarely exceed ~10 code points.
 */
#define NFC_SEG_MAX 128

typedef struct {
    uint32_t cp;
    int      ccc;
} NFCCodePoint;

/* --- UTF-8 encode/decode helpers --- */

static uint32_t utf8_decode_raw(const unsigned char *p, int n)
{
    if (1 == n) return p[0];
    if (2 == n) return ((uint32_t)(p[0] & 0x1F) << 6)
                     |  (uint32_t)(p[1] & 0x3F);
    if (3 == n) return ((uint32_t)(p[0] & 0x0F) << 12)
                     | ((uint32_t)(p[1] & 0x3F) << 6)
                     |  (uint32_t)(p[2] & 0x3F);
    return ((uint32_t)(p[0] & 0x07) << 18)
         | ((uint32_t)(p[1] & 0x3F) << 12)
         | ((uint32_t)(p[2] & 0x3F) << 6)
         |  (uint32_t)(p[3] & 0x3F);
}

static int utf8_is_valid_scalar(uint32_t cp, int n)
{
    if ((2 == n && cp < 0x80)
       || (3 == n && cp < 0x800)
       || (4 == n && cp < 0x10000)
       || cp > 0x10FFFF
       || (cp >= 0xD800 && cp <= 0xDFFF))
        return 0;
    return 1;
}

static uint32_t utf8_Decode(const unsigned char **pp, const unsigned char *pEnd)
{
    const unsigned char *p = *pp;
    if (p >= pEnd) return UNI_EOF;

    int n = utf8_FirstByte[*p];
    if (n <= 0 || n >= UTF8_CONTINUE) { (*pp)++; return UNI_EOF; }
    if (p + n > pEnd) { *pp = pEnd; return UNI_EOF; }
    for (int i = 1; i < n; i++) {
        if (UTF8_CONTINUE != utf8_FirstByte[p[i]]) { (*pp)++; return UNI_EOF; }
    }

    uint32_t cp = utf8_decode_raw(p, n);
    if (!utf8_is_valid_scalar(cp, n)) { (*pp)++; return UNI_EOF; }
    *pp = p + n;
    return cp;
}

static int utf8_Encode(uint32_t cp, unsigned char *buf)
{
    if (cp < 0x80) {
        buf[0] = (unsigned char)cp;
        return 1;
    } else if (cp < 0x800) {
        buf[0] = (unsigned char)(0xC0 | (cp >> 6));
        buf[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        buf[0] = (unsigned char)(0xE0 | (cp >> 12));
        buf[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp <= 0x10FFFF) {
        buf[0] = (unsigned char)(0xF0 | (cp >> 18));
        buf[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (unsigned char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

/* --- DFA traversal --- */

/* Run integer DFA with unsigned short SBT (RUN/COPY format). */
static int RunIntegerDFA_u16(
    const unsigned char *itt,
    const unsigned short *sot,
    const unsigned short *sbt,
    int nStartState, int nAcceptStart, int nDefault,
    const unsigned char *pStart, const unsigned char *pEnd)
{
    int iState = nStartState;
    const unsigned char *p = pStart;
    while (p < pEnd && iState < nAcceptStart) {
        unsigned char ch = *p++;
        unsigned char iColumn = itt[ch];
        unsigned short iOffset = sot[iState];
        for (;;) {
            int y = sbt[iOffset];
            if (y < 128) {
                if (iColumn < y) { iState = sbt[iOffset + 1]; break; }
                iColumn = (unsigned char)(iColumn - y);
                iOffset += 2;
            } else {
                y = 256 - y;
                if (iColumn < y) { iState = sbt[iOffset + iColumn + 1]; break; }
                iColumn = (unsigned char)(iColumn - y);
                iOffset = (unsigned short)(iOffset + y + 1);
            }
        }
    }
    return (iState >= nAcceptStart) ? iState - nAcceptStart : nDefault;
}

/* Run integer DFA with unsigned char SBT. */
static int RunIntegerDFA_u8(
    const unsigned char *itt,
    const unsigned short *sot,
    const unsigned char *sbt,
    int nStartState, int nAcceptStart, int nDefault,
    const unsigned char *pStart, const unsigned char *pEnd)
{
    int iState = nStartState;
    const unsigned char *p = pStart;
    while (p < pEnd && iState < nAcceptStart) {
        unsigned char ch = *p++;
        unsigned char iColumn = itt[ch];
        unsigned short iOffset = sot[iState];
        for (;;) {
            int y = sbt[iOffset];
            if (y < 128) {
                if (iColumn < y) { iState = sbt[iOffset + 1]; break; }
                iColumn = (unsigned char)(iColumn - y);
                iOffset += 2;
            } else {
                y = 256 - y;
                if (iColumn < y) { iState = sbt[iOffset + iColumn + 1]; break; }
                iColumn = (unsigned char)(iColumn - y);
                iOffset = (unsigned short)(iOffset + y + 1);
            }
        }
    }
    return (iState >= nAcceptStart) ? iState - nAcceptStart : nDefault;
}

static int GetCCC(const unsigned char *pStart, const unsigned char *pEnd)
{
    return RunIntegerDFA_u16(tr_ccc_itt, tr_ccc_sot, tr_ccc_sbt,
        TR_CCC_START_STATE, TR_CCC_ACCEPTING_STATES_START, 0,
        pStart, pEnd);
}

static int GetNFCQC(const unsigned char *pStart, const unsigned char *pEnd)
{
    return RunIntegerDFA_u8(tr_nfcqc_itt, tr_nfcqc_sot, tr_nfcqc_sbt,
        TR_NFCQC_START_STATE, TR_NFCQC_ACCEPTING_STATES_START, 0,
        pStart, pEnd);
}

static const co_string_desc *GetNFD(const unsigned char *p, int *bXor)
{
    unsigned short iState = TR_NFD_START_STATE;
    do {
        unsigned char ch = *p++;
        unsigned char iColumn = tr_nfd_itt[ch];
        unsigned short iOffset = tr_nfd_sot[iState];
        for (;;) {
            int y = tr_nfd_sbt[iOffset];
            if (y < 128) {
                if (iColumn < y) { iState = tr_nfd_sbt[iOffset + 1]; break; }
                iColumn = (unsigned char)(iColumn - y);
                iOffset += 2;
            } else {
                y = 256 - y;
                if (iColumn < y) { iState = tr_nfd_sbt[iOffset + iColumn + 1]; break; }
                iColumn = (unsigned char)(iColumn - y);
                iOffset = (unsigned short)(iOffset + y + 1);
            }
        }
    } while (iState < TR_NFD_ACCEPTING_STATES_START);

    int idx = iState - TR_NFD_ACCEPTING_STATES_START;
    if (TR_NFD_DEFAULT == idx) {
        *bXor = 0;
        return NULL;
    }
    *bXor = (TR_NFD_XOR_START <= idx);
    return tr_nfd_ott + idx - 1;
}

static uint32_t ComposeViaTable(const unsigned char *pStarter, int nStarterBytes,
                                const unsigned char *pCombining, int nCombiningBytes)
{
    int iState = TR_NFC_COMPOSE_START_STATE;

    for (int i = 0; i < nStarterBytes && iState < TR_NFC_COMPOSE_ACCEPTING_STATES_START; i++) {
        unsigned char iColumn = tr_nfc_compose_itt[pStarter[i]];
        unsigned short iOffset = tr_nfc_compose_sot[iState];
        for (;;) {
            int y = tr_nfc_compose_sbt[iOffset];
            if (y < 128) {
                if (iColumn < y) { iState = tr_nfc_compose_sbt[iOffset + 1]; break; }
                iColumn = (unsigned char)(iColumn - y);
                iOffset += 2;
            } else {
                y = 256 - y;
                if (iColumn < y) { iState = tr_nfc_compose_sbt[iOffset + iColumn + 1]; break; }
                iColumn = (unsigned char)(iColumn - y);
                iOffset = (unsigned short)(iOffset + y + 1);
            }
        }
    }

    for (int i = 0; i < nCombiningBytes && iState < TR_NFC_COMPOSE_ACCEPTING_STATES_START; i++) {
        unsigned char iColumn = tr_nfc_compose_itt[pCombining[i]];
        unsigned short iOffset = tr_nfc_compose_sot[iState];
        for (;;) {
            int y = tr_nfc_compose_sbt[iOffset];
            if (y < 128) {
                if (iColumn < y) { iState = tr_nfc_compose_sbt[iOffset + 1]; break; }
                iColumn = (unsigned char)(iColumn - y);
                iOffset += 2;
            } else {
                y = 256 - y;
                if (iColumn < y) { iState = tr_nfc_compose_sbt[iOffset + iColumn + 1]; break; }
                iColumn = (unsigned char)(iColumn - y);
                iOffset = (unsigned short)(iOffset + y + 1);
            }
        }
    }

    if (iState < TR_NFC_COMPOSE_ACCEPTING_STATES_START) return 0;
    int idx = iState - TR_NFC_COMPOSE_ACCEPTING_STATES_START;
    if (0 == idx) return 0;
    return tr_nfc_compose_nfc_compose_result[idx];
}

static uint32_t Compose(uint32_t cp1, uint32_t cp2)
{
    /* Hangul L + V -> LV */
    if (HANGUL_LBASE <= cp1 && cp1 < HANGUL_LBASE + HANGUL_LCOUNT
        && HANGUL_VBASE <= cp2 && cp2 < HANGUL_VBASE + HANGUL_VCOUNT) {
        return HANGUL_SBASE
             + (cp1 - HANGUL_LBASE) * HANGUL_NCOUNT
             + (cp2 - HANGUL_VBASE) * HANGUL_TCOUNT;
    }
    /* Hangul LV + T -> LVT */
    if (HANGUL_SBASE <= cp1 && cp1 < HANGUL_SBASE + HANGUL_SCOUNT
        && 0 == ((cp1 - HANGUL_SBASE) % HANGUL_TCOUNT)
        && HANGUL_TBASE < cp2 && cp2 < HANGUL_TBASE + HANGUL_TCOUNT) {
        return cp1 + (cp2 - HANGUL_TBASE);
    }
    /* Table lookup via DFA. */
    unsigned char buf1[4], buf2[4];
    int n1 = utf8_Encode(cp1, buf1);
    int n2 = utf8_Encode(cp2, buf2);
    if (n1 <= 0 || n2 <= 0) return 0;
    return ComposeViaTable(buf1, n1, buf2, n2);
}

/* --- Decomposition --- */

static void DecomposeOne(uint32_t cp, NFCCodePoint *buf, int *n, int maxN)
{
    /* Hangul syllable decomposition. */
    if (HANGUL_SBASE <= cp && cp < HANGUL_SBASE + HANGUL_SCOUNT) {
        int sIndex = (int)(cp - HANGUL_SBASE);
        uint32_t l = HANGUL_LBASE + sIndex / HANGUL_NCOUNT;
        uint32_t v = HANGUL_VBASE + (sIndex % HANGUL_NCOUNT) / HANGUL_TCOUNT;
        uint32_t t = HANGUL_TBASE + sIndex % HANGUL_TCOUNT;

        if (*n < maxN) { buf[*n].cp = l; buf[*n].ccc = 0; (*n)++; }
        if (*n < maxN) { buf[*n].cp = v; buf[*n].ccc = 0; (*n)++; }
        if (t != HANGUL_TBASE && *n < maxN) { buf[*n].cp = t; buf[*n].ccc = 0; (*n)++; }
        return;
    }

    /* Table lookup. */
    unsigned char encoded[4];
    int nBytes = utf8_Encode(cp, encoded);
    if (nBytes <= 0) return;

    int bXor;
    const co_string_desc *sd = GetNFD(encoded, &bXor);
    if (NULL == sd) {
        /* No decomposition — emit as-is. */
        if (*n < maxN) {
            buf[*n].cp = cp;
            buf[*n].ccc = GetCCC(encoded, encoded + nBytes);
            (*n)++;
        }
        return;
    }

    /* Build decomposed UTF-8. */
    unsigned char decomposed[32];
    size_t nDecomp = sd->n_bytes;
    if (nDecomp > sizeof(decomposed)) nDecomp = sizeof(decomposed);

    if (bXor) {
        for (size_t i = 0; i < nDecomp; i++)
            decomposed[i] = encoded[i] ^ sd->p[i];
    } else {
        memcpy(decomposed, sd->p, nDecomp);
    }

    /* Parse decomposed UTF-8 into code points. */
    const unsigned char *dp = decomposed;
    const unsigned char *dpEnd = decomposed + nDecomp;
    while (dp < dpEnd && *n < maxN) {
        uint32_t dcp = utf8_Decode(&dp, dpEnd);
        if (UNI_EOF == dcp) break;

        unsigned char enc3[4];
        int nb3 = utf8_Encode(dcp, enc3);
        buf[*n].cp = dcp;
        buf[*n].ccc = (nb3 > 0) ? GetCCC(enc3, enc3 + nb3) : 0;
        (*n)++;
    }
}

/* Canonical ordering: stable insertion sort by CCC. */
static void CanonicalOrder(NFCCodePoint *buf, int n)
{
    for (int i = 1; i < n; i++) {
        if (buf[i].ccc != 0) {
            NFCCodePoint tmp = buf[i];
            int j = i;
            while (j > 0 && buf[j-1].ccc > tmp.ccc && buf[j-1].ccc != 0) {
                buf[j] = buf[j-1];
                j--;
            }
            buf[j] = tmp;
        }
    }
}

/* Canonical composition step. */
static void CanonicalCompose(NFCCodePoint *buf, int *n)
{
    if (*n < 2) return;

    int starterIdx = -1;
    for (int i = 0; i < *n; i++) {
        if (0 == buf[i].ccc) { starterIdx = i; break; }
    }
    if (starterIdx < 0) return;

    int lastCCC = -1;
    for (int i = starterIdx + 1; i < *n; i++) {
        int ccc = buf[i].ccc;
        int blocked = (lastCCC != -1 && lastCCC >= ccc && ccc != 0);

        if (!blocked) {
            uint32_t composed = Compose(buf[starterIdx].cp, buf[i].cp);
            if (0 != composed) {
                buf[starterIdx].cp = composed;
                for (int j = i; j < *n - 1; j++) buf[j] = buf[j+1];
                (*n)--;
                i--;
                lastCCC = -1;
                continue;
            }
        }

        if (0 == ccc) {
            starterIdx = i;
            lastCCC = -1;
        } else {
            lastCCC = ccc;
        }
    }
}

/* --- UTF-8 validation helper --- */

/* Return byte length of a valid UTF-8 code point at p, or 0 if invalid. */
static int utf8_cplen(const unsigned char *p, const unsigned char *pEnd)
{
    int n = utf8_FirstByte[*p];
    if (n <= 0 || n >= UTF8_CONTINUE || p + n > pEnd) return 0;
    for (int i = 1; i < n; i++) {
        if (UTF8_CONTINUE != utf8_FirstByte[p[i]]) return 0;
    }
    uint32_t cp = utf8_decode_raw(p, n);
    return utf8_is_valid_scalar(cp, n) ? n : 0;
}

/* --- Segment normalizer --- */

/* Normalize a single combining character sequence into dst.
 * Returns bytes written.
 */
static size_t NormalizeSegment(const unsigned char *src, size_t nSrc,
                               unsigned char *dst, size_t nDstMax)
{
    NFCCodePoint cps[NFC_SEG_MAX];
    int nCps = 0;

    const unsigned char *p = src;
    const unsigned char *pEnd = src + nSrc;
    while (p < pEnd && nCps < NFC_SEG_MAX) {
        uint32_t cp = utf8_Decode(&p, pEnd);
        if (UNI_EOF == cp) continue;
        DecomposeOne(cp, cps, &nCps, NFC_SEG_MAX);
    }

    CanonicalOrder(cps, nCps);
    CanonicalCompose(cps, &nCps);

    size_t nOut = 0;
    for (int i = 0; i < nCps; i++) {
        unsigned char enc[4];
        int nb = utf8_Encode(cps[i].cp, enc);
        if (nb > 0 && nOut + (size_t)nb <= nDstMax) {
            memcpy(dst + nOut, enc, nb);
            nOut += nb;
        }
    }
    return nOut;
}

/* --- Public API --- */

int utf_nfc_is_nfc(const unsigned char *src, size_t nSrc)
{
    const unsigned char *p = src;
    const unsigned char *pEnd = src + nSrc;
    int lastCCC = 0;

    while (p < pEnd) {
        /* ASCII fast path: always NFC_QC=Yes, CCC=0. */
        if (*p < 0x80) {
            lastCCC = 0;
            p++;
            continue;
        }

        int n = utf8_cplen(p, pEnd);
        if (0 == n) return 0;

        int qc = GetNFCQC(p, p + n);
        if (0 != qc) return 0;  /* No or Maybe */

        int ccc = GetCCC(p, p + n);
        if (ccc != 0 && lastCCC > ccc) return 0;

        lastCCC = ccc;
        p += n;
    }
    return 1;
}

void utf_nfc_normalize(const unsigned char *src, size_t nSrc,
                       unsigned char *dst, size_t nDstMax, size_t *pnDst)
{
    *pnDst = 0;
    const unsigned char *p = src;
    const unsigned char *pEnd = src + nSrc;
    size_t nOut = 0;

    const unsigned char *copyFrom = src;    /* start of unwritten clean data */
    const unsigned char *lastStarter = src; /* last CCC=0 position in clean run */
    int lastCCC = 0;

    while (p < pEnd) {
        /* ASCII fast path: CCC=0, NFC_QC=Yes. */
        if (*p < 0x80) {
            lastStarter = p;
            lastCCC = 0;
            p++;
            continue;
        }

        int n = utf8_cplen(p, pEnd);
        if (0 == n) {
            /* Invalid UTF-8: skip byte, reset CCC tracking. */
            p++;
            lastCCC = 0;
            continue;
        }

        int qc = GetNFCQC(p, p + n);
        int ccc = GetCCC(p, p + n);

        if (0 == qc && (0 == ccc || lastCCC <= ccc)) {
            /* Clean code point — pass through. */
            if (0 == ccc) lastStarter = p;
            lastCCC = ccc;
            p += n;
            continue;
        }

        /* NFC violation. Dirty segment starts at lastStarter. */

        /* Copy clean prefix [copyFrom, lastStarter) to output. */
        size_t cleanLen = (size_t)(lastStarter - copyFrom);
        if (cleanLen > 0 && nOut + cleanLen <= nDstMax) {
            memcpy(dst + nOut, copyFrom, cleanLen);
            nOut += cleanLen;
        }

        /* Skip past the problem code point. */
        p += n;

        /* Scan forward to find the end of the dirty segment:
         * the next starter (CCC=0) with NFC_QC=Yes.
         */
        while (p < pEnd) {
            if (*p < 0x80) break;  /* ASCII = clean starter */
            int n2 = utf8_cplen(p, pEnd);
            if (0 == n2) { p++; continue; }
            int ccc2 = GetCCC(p, p + n2);
            if (0 == ccc2) {
                int qc2 = GetNFCQC(p, p + n2);
                if (0 == qc2) break;  /* Clean starter: end of dirty segment */
            }
            p += n2;
        }

        /* Normalize [lastStarter, p). */
        size_t segLen = (size_t)(p - lastStarter);
        nOut += NormalizeSegment(lastStarter, segLen,
                                dst + nOut,
                                (nOut < nDstMax) ? nDstMax - nOut : 0);

        copyFrom = p;
        lastStarter = p;
        lastCCC = 0;
    }

    /* Copy remaining clean tail. */
    size_t tailLen = (size_t)(pEnd - copyFrom);
    if (tailLen > 0 && nOut + tailLen <= nDstMax) {
        memcpy(dst + nOut, copyFrom, tailLen);
        nOut += tailLen;
    }

    *pnDst = nOut;
}
