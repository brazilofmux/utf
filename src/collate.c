/*
 * collate.c — Unicode Collation Algorithm (UCA) per UTS #10.
 *
 * Ported from TinyMUX mux/lib/utf8_collate.cpp (C++) to pure C.
 *
 * DUCET-based multi-level string comparison with implicit weights
 * for CJK and unassigned code points.  Unicode 16.0.
 */

#include "utf/collate.h"
#include "utf/nfc.h"
#include "utf/utf_tables.h"
#include <string.h>
#include <stdint.h>

#define UNI_EOF ((uint32_t)-1)
#define UTF8_CONTINUE 5

/* CE weight unpacking from uint32_t.
 *   Bit  31:    variable flag
 *   Bits 30-16: primary weight (15 bits)
 *   Bits 15-5:  secondary weight (11 bits)
 *   Bits 4-0:   tertiary weight (5 bits)
 */
#define CE_PRIMARY(w)   (((w) >> 16) & 0x7FFF)
#define CE_SECONDARY(w) (((w) >> 5) & 0x07FF)
#define CE_TERTIARY(w)  ((w) & 0x1F)

/* CE buffer size for comparison.  256 CEs supports strings up to ~200
 * characters, which covers the vast majority of real-world comparisons.
 * Sort key generation uses a larger buffer (MAX_SORTKEY_CES).
 */
#define MAX_CMP_CES 256
#define MAX_SORTKEY_CES 4096

/* --- UTF-8 helpers --- */

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
    if ((2 == n && cp < 0x80) || (3 == n && cp < 0x800)
       || (4 == n && cp < 0x10000) || cp > 0x10FFFF
       || (cp >= 0xD800 && cp <= 0xDFFF))
        return 0;
    return 1;
}

static const unsigned char *utf8_advance_c(const unsigned char *p,
                                           const unsigned char *pEnd)
{
    if (p >= pEnd) return p;
    int n = utf8_FirstByte[*p];
    if (n < 1 || n >= UTF8_CONTINUE) return p + 1;
    for (int i = 1; i < n; i++) {
        if (p + i >= pEnd || UTF8_CONTINUE != utf8_FirstByte[p[i]])
            return p + 1;
    }
    uint32_t cp = utf8_decode_raw(p, n);
    if (!utf8_is_valid_scalar(cp, n)) return p + 1;
    const unsigned char *pNext = p + n;
    return (pNext <= pEnd) ? pNext : pEnd;
}

static uint32_t utf8_decode_c(const unsigned char *p, const unsigned char *pEnd)
{
    if (p >= pEnd) return UNI_EOF;
    int n = utf8_FirstByte[*p];
    if (n < 1 || n >= UTF8_CONTINUE) return UNI_EOF;
    if (p + n > pEnd) return UNI_EOF;
    for (int i = 1; i < n; i++) {
        if (UTF8_CONTINUE != utf8_FirstByte[p[i]]) return UNI_EOF;
    }
    uint32_t cp = utf8_decode_raw(p, n);
    return utf8_is_valid_scalar(cp, n) ? cp : UNI_EOF;
}

/* --- DFA lookups --- */

static int GetDUCET(const unsigned char *p, const unsigned char *pEnd)
{
    int iState = TR_DUCET_START_STATE;
    while (p < pEnd) {
        unsigned char ch = *p++;
        int iColumn = tr_ducet_itt[ch];
        int iOffset = tr_ducet_sot[iState];
        for (;;) {
            int y = tr_ducet_sbt[iOffset];
            if (y < 128) {
                if (iColumn < y) { iState = tr_ducet_sbt[iOffset + 1]; break; }
                iColumn -= y; iOffset += 2;
            } else {
                y = 256 - y;
                if (iColumn < y) { iState = tr_ducet_sbt[iOffset + iColumn + 1]; break; }
                iColumn -= y; iOffset += y + 1;
            }
        }
    }
    return (iState >= TR_DUCET_ACCEPTING_STATES_START)
         ? iState - TR_DUCET_ACCEPTING_STATES_START : 0;
}

static int GetContraction(const unsigned char *p1, const unsigned char *p1End,
                          const unsigned char *p2, const unsigned char *p2End)
{
    int iState = TR_DUCET_CONTRACT_START_STATE;

    while (p1 < p1End && iState < TR_DUCET_CONTRACT_ACCEPTING_STATES_START) {
        unsigned char ch = *p1++;
        int iColumn = tr_ducet_contract_itt[ch];
        int iOffset = tr_ducet_contract_sot[iState];
        for (;;) {
            int y = tr_ducet_contract_sbt[iOffset];
            if (y < 128) {
                if (iColumn < y) { iState = tr_ducet_contract_sbt[iOffset + 1]; break; }
                iColumn -= y; iOffset += 2;
            } else {
                y = 256 - y;
                if (iColumn < y) { iState = tr_ducet_contract_sbt[iOffset + iColumn + 1]; break; }
                iColumn -= y; iOffset += y + 1;
            }
        }
    }

    while (p2 < p2End && iState < TR_DUCET_CONTRACT_ACCEPTING_STATES_START) {
        unsigned char ch = *p2++;
        int iColumn = tr_ducet_contract_itt[ch];
        int iOffset = tr_ducet_contract_sot[iState];
        for (;;) {
            int y = tr_ducet_contract_sbt[iOffset];
            if (y < 128) {
                if (iColumn < y) { iState = tr_ducet_contract_sbt[iOffset + 1]; break; }
                iColumn -= y; iOffset += 2;
            } else {
                y = 256 - y;
                if (iColumn < y) { iState = tr_ducet_contract_sbt[iOffset + iColumn + 1]; break; }
                iColumn -= y; iOffset += y + 1;
            }
        }
    }

    if (iState >= TR_DUCET_CONTRACT_ACCEPTING_STATES_START) {
        int idx = iState - TR_DUCET_CONTRACT_ACCEPTING_STATES_START;
        if (0 != idx) return (int)tr_ducet_contract_nfc_compose_result[idx];
    }
    return 0;
}

/* --- Implicit weights (UCA Section 10.1) --- */

typedef struct { uint32_t start; uint32_t end; unsigned short base; } ImplicitRange;

static const ImplicitRange s_ImplicitRanges[] = {
    { 0x4E00,  0x9FFF,  0xFB40 },
    { 0xF900,  0xFAFF,  0xFB40 },
    { 0x3400,  0x4DBF,  0xFB80 },
    { 0x20000, 0x2A6DF, 0xFB80 },
    { 0x2A700, 0x2B73F, 0xFB80 },
    { 0x2B740, 0x2B81F, 0xFB80 },
    { 0x2B820, 0x2CEAF, 0xFB80 },
    { 0x2CEB0, 0x2EBEF, 0xFB80 },
    { 0x30000, 0x3134F, 0xFB80 },
    { 0x31350, 0x323AF, 0xFB80 },
    { 0x2EBF0, 0x2F7FF, 0xFB80 },
    { 0x17000, 0x18AFF, 0xFB00 },
    { 0x18D00, 0x18D7F, 0xFB00 },
    { 0x1B170, 0x1B2FF, 0xFB01 },
    { 0x18B00, 0x18CFF, 0xFB02 },
};

static void ImplicitWeight(uint32_t cp, unsigned short *aaaa, unsigned short *bbbb)
{
    unsigned short base = 0xFBC0;
    int nRanges = (int)(sizeof(s_ImplicitRanges) / sizeof(s_ImplicitRanges[0]));
    for (int i = 0; i < nRanges; i++) {
        if (cp >= s_ImplicitRanges[i].start && cp <= s_ImplicitRanges[i].end) {
            base = s_ImplicitRanges[i].base;
            break;
        }
    }
    *aaaa = base + (unsigned short)(cp >> 15);
    *bbbb = (unsigned short)((cp & 0x7FFF) | 0x8000);
}

/* --- Per-codepoint CE extraction --- */

/* Extract CEs for the next code point (or contraction) at *pp.
 * Advances *pp past the consumed input bytes.
 * Returns number of CEs written to ces[].
 */
static int ExtractCEs(const unsigned char **pp, const unsigned char *pEnd,
                      uint32_t *ces, int maxCEs)
{
    const unsigned char *p = *pp;
    int nCEs = 0;

    /* ASCII fast path: no contractions, single-byte DUCET lookup. */
    if (*p < 0x80) {
        int ceIndex = GetDUCET(p, p + 1);
        if (0 != ceIndex) {
            int start = ducet_ce_offset[ceIndex];
            int end   = ducet_ce_offset[ceIndex + 1];
            for (int i = start; i < end && nCEs < maxCEs; i++)
                ces[nCEs++] = ducet_ce_weights[i];
        } else {
            unsigned short aaaa, bbbb;
            ImplicitWeight((uint32_t)*p, &aaaa, &bbbb);
            if (nCEs < maxCEs)
                ces[nCEs++] = ((uint32_t)aaaa << 16) | ((uint32_t)0x0020 << 5) | 0x0002;
            if (nCEs < maxCEs)
                ces[nCEs++] = (uint32_t)bbbb << 16;
        }
        *pp = p + 1;
        return nCEs;
    }

    /* Non-ASCII: advance, try contraction, then single-cp DUCET. */
    const unsigned char *pNext = utf8_advance_c(p, pEnd);
    int ceIndex = 0;
    const unsigned char *pConsumed = pNext;

    /* Contraction check: only if the lead byte maps to a non-default
     * column in the contraction DFA.  Column 0 is the default and can
     * never reach an accepting state, so skip the DFA entirely.
     */
    if (tr_ducet_contract_itt[*p] != 0 && pNext < pEnd) {
        const unsigned char *pNext2 = utf8_advance_c(pNext, pEnd);
        ceIndex = GetContraction(p, pNext, pNext, pNext2);
        if (0 != ceIndex) pConsumed = pNext2;
    }

    if (0 == ceIndex) ceIndex = GetDUCET(p, pNext);

    if (0 != ceIndex) {
        int start = ducet_ce_offset[ceIndex];
        int end   = ducet_ce_offset[ceIndex + 1];
        for (int i = start; i < end && nCEs < maxCEs; i++)
            ces[nCEs++] = ducet_ce_weights[i];
    } else {
        uint32_t cp = utf8_decode_c(p, pNext);
        if (UNI_EOF != cp) {
            unsigned short aaaa, bbbb;
            ImplicitWeight(cp, &aaaa, &bbbb);
            if (nCEs < maxCEs)
                ces[nCEs++] = ((uint32_t)aaaa << 16) | ((uint32_t)0x0020 << 5) | 0x0002;
            if (nCEs < maxCEs)
                ces[nCEs++] = (uint32_t)bbbb << 16;
        }
    }
    *pp = pConsumed;
    return nCEs;
}

/* --- Latin CE cache --- */

/* Precomputed CE for U+0000..U+017F (Basic Latin + Latin-1 Supplement +
 * Latin Extended-A).  Most characters in this range have exactly one CE
 * in DUCET.  A value of 0 means "use slow path" (multi-CE or unmapped).
 *
 * This is the key fast path: Latin text skips DFA traversal, contraction
 * checks, and UTF-8 validation entirely — one table lookup per character.
 */
#define LATIN_CE_LIMIT 0x180
static uint32_t s_latin_ce[LATIN_CE_LIMIT];
static int      s_latin_ce_init;

static void InitLatinCache(void)
{
    for (int cp = 0; cp < LATIN_CE_LIMIT; cp++) {
        unsigned char buf[2];
        int n;
        if (cp < 0x80) {
            buf[0] = (unsigned char)cp;
            n = 1;
        } else {
            buf[0] = (unsigned char)(0xC0 | (cp >> 6));
            buf[1] = (unsigned char)(0x80 | (cp & 0x3F));
            n = 2;
        }
        int idx = GetDUCET(buf, buf + n);
        if (0 != idx) {
            int start = ducet_ce_offset[idx];
            int end   = ducet_ce_offset[idx + 1];
            if (end - start == 1) {
                s_latin_ce[cp] = ducet_ce_weights[start];
                continue;
            }
        }
        s_latin_ce[cp] = 0;
    }
    s_latin_ce_init = 1;
}

/* --- CE collection (full string) --- */

static int CollectCEs(const unsigned char *src, size_t nSrc,
                      uint32_t *ces, int maxCEs)
{
    if (!s_latin_ce_init) InitLatinCache();

    const unsigned char *p = src;
    const unsigned char *pEnd = src + nSrc;
    int nCEs = 0;

    while (p < pEnd && nCEs < maxCEs) {
        /* ASCII fast path: single table lookup, no DFA. */
        if (*p < 0x80) {
            uint32_t ce = s_latin_ce[*p];
            if (0 != ce) {
                ces[nCEs++] = ce;
                p++;
                continue;
            }
            /* Rare: ASCII with 0 or multiple CEs — fall through. */
        }
        /* Latin 2-byte fast path: U+0080..U+017F (lead bytes 0xC2..0xC5).
         * Direct decode + table lookup — no DFA, no contraction check.
         */
        else if ((unsigned)(*p - 0xC2) <= (0xC5 - 0xC2)
                 && p + 1 < pEnd && (p[1] & 0xC0) == 0x80) {
            uint32_t cp = (uint32_t)((*p & 0x1F) << 6) | (p[1] & 0x3F);
            uint32_t ce = s_latin_ce[cp];
            if (0 != ce) {
                ces[nCEs++] = ce;
                p += 2;
                continue;
            }
        }
        /* CJK Unified Ideographs fast path: U+4E00..U+9FFF (lead 0xE4..0xE9).
         * These always use implicit weights — skip the DFA entirely.
         */
        else if (*p >= 0xE4 && *p <= 0xE9
                 && p + 2 < pEnd
                 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            uint32_t cp = ((uint32_t)(*p & 0x0F) << 12)
                        | ((uint32_t)(p[1] & 0x3F) << 6)
                        | (uint32_t)(p[2] & 0x3F);
            if (cp >= 0x4E00 && cp <= 0x9FFF && nCEs + 1 < maxCEs) {
                unsigned short aaaa = 0xFB40 + (unsigned short)(cp >> 15);
                unsigned short bbbb = (unsigned short)((cp & 0x7FFF) | 0x8000);
                ces[nCEs++] = ((uint32_t)aaaa << 16) | ((uint32_t)0x0020 << 5) | 0x0002;
                ces[nCEs++] = (uint32_t)bbbb << 16;
                p += 3;
                continue;
            }
        }
        nCEs += ExtractCEs(&p, pEnd, ces + nCEs, maxCEs - nCEs);
    }
    return nCEs;
}

/* --- Latin fast-path comparison --- */

/* Decode next Latin codepoint and return its cached CE.
 * Returns 0 if the byte is non-Latin or has no single-CE cache entry.
 */
static inline uint32_t NextLatinCE(const unsigned char **pp,
                                    const unsigned char *pEnd)
{
    const unsigned char *p = *pp;
    if (*p < 0x80) {
        uint32_t ce = s_latin_ce[*p];
        if (0 != ce) { *pp = p + 1; return ce; }
        return 0;
    }
    if ((unsigned)(*p - 0xC2) <= (0xC5 - 0xC2)
        && p + 1 < pEnd && (p[1] & 0xC0) == 0x80) {
        uint32_t cp = (uint32_t)((*p & 0x1F) << 6) | (p[1] & 0x3F);
        uint32_t ce = s_latin_ce[cp];
        if (0 != ce) { *pp = p + 2; return ce; }
    }
    return 0;
}

/* Try fast Latin comparison.  If both strings are entirely Latin with
 * single-CE characters, compare inline without collecting CE arrays.
 * Returns 1 if the fast path handled it (result in *pResult), 0 if
 * the caller must fall back to the full UCA path.
 *
 * For single-CE Latin, all three weight levels are non-zero, so UCA's
 * three-pass comparison reduces to a single element-by-element pass
 * with recorded secondary/tertiary differences.
 */
static int FastLatinCmp(const unsigned char *a, size_t nA,
                        const unsigned char *b, size_t nB,
                        int *pResult)
{
    if (!s_latin_ce_init) InitLatinCache();

    const unsigned char *pa = a, *paEnd = a + nA;
    const unsigned char *pb = b, *pbEnd = b + nB;
    int secDiff = 0, tertDiff = 0;

    while (pa < paEnd && pb < pbEnd) {
        uint32_t ceA = NextLatinCE(&pa, paEnd);
        if (0 == ceA) return 0;
        uint32_t ceB = NextLatinCE(&pb, pbEnd);
        if (0 == ceB) return 0;

        unsigned short pA = CE_PRIMARY(ceA), pB = CE_PRIMARY(ceB);
        if (pA != pB) { *pResult = (pA < pB) ? -1 : 1; return 1; }

        if (0 == secDiff) {
            unsigned short sA = CE_SECONDARY(ceA), sB = CE_SECONDARY(ceB);
            if (sA != sB) { secDiff = (sA < sB) ? -1 : 1; }
            else if (0 == tertDiff) {
                unsigned char tA = CE_TERTIARY(ceA), tB = CE_TERTIARY(ceB);
                if (tA != tB) { tertDiff = (tA < tB) ? -1 : 1; }
            }
        }
    }

    /* One or both strings exhausted at level 1.
     * Any remaining characters have non-zero primary (Latin guarantee).
     */
    if (pa < paEnd) {
        if (0 == NextLatinCE(&pa, paEnd)) return 0;
        *pResult = 1; return 1;
    }
    if (pb < pbEnd) {
        if (0 == NextLatinCE(&pb, pbEnd)) return 0;
        *pResult = -1; return 1;
    }

    if (0 != secDiff) { *pResult = secDiff; return 1; }
    if (0 != tertDiff) { *pResult = tertDiff; return 1; }

    *pResult = 0;
    return 1;
}

/* --- Public API --- */

int utf_collate_cmp(const unsigned char *a, size_t nA,
                    const unsigned char *b, size_t nB)
{
    /* Fast path: Latin-only strings compared inline. */
    int fastResult;
    if (FastLatinCmp(a, nA, b, nB, &fastResult))
        return fastResult;

    uint32_t cesA[MAX_CMP_CES], cesB[MAX_CMP_CES];
    int nCEsA = CollectCEs(a, nA, cesA, MAX_CMP_CES);
    int nCEsB = CollectCEs(b, nB, cesB, MAX_CMP_CES);
    int iA, iB;

    /* Level 1: primary. */
    iA = 0; iB = 0;
    for (;;) {
        while (iA < nCEsA && 0 == CE_PRIMARY(cesA[iA])) iA++;
        while (iB < nCEsB && 0 == CE_PRIMARY(cesB[iB])) iB++;
        if (iA >= nCEsA || iB >= nCEsB) break;
        unsigned short pA = CE_PRIMARY(cesA[iA]);
        unsigned short pB = CE_PRIMARY(cesB[iB]);
        if (pA < pB) return -1;
        if (pA > pB) return 1;
        iA++; iB++;
    }
    while (iA < nCEsA && 0 == CE_PRIMARY(cesA[iA])) iA++;
    while (iB < nCEsB && 0 == CE_PRIMARY(cesB[iB])) iB++;
    if (iA < nCEsA) return 1;
    if (iB < nCEsB) return -1;

    /* Level 2: secondary. */
    iA = 0; iB = 0;
    for (;;) {
        while (iA < nCEsA && 0 == CE_SECONDARY(cesA[iA])) iA++;
        while (iB < nCEsB && 0 == CE_SECONDARY(cesB[iB])) iB++;
        if (iA >= nCEsA || iB >= nCEsB) break;
        unsigned short sA = CE_SECONDARY(cesA[iA]);
        unsigned short sB = CE_SECONDARY(cesB[iB]);
        if (sA < sB) return -1;
        if (sA > sB) return 1;
        iA++; iB++;
    }
    while (iA < nCEsA && 0 == CE_SECONDARY(cesA[iA])) iA++;
    while (iB < nCEsB && 0 == CE_SECONDARY(cesB[iB])) iB++;
    if (iA < nCEsA) return 1;
    if (iB < nCEsB) return -1;

    /* Level 3: tertiary. */
    iA = 0; iB = 0;
    for (;;) {
        while (iA < nCEsA && 0 == CE_TERTIARY(cesA[iA])) iA++;
        while (iB < nCEsB && 0 == CE_TERTIARY(cesB[iB])) iB++;
        if (iA >= nCEsA || iB >= nCEsB) break;
        unsigned char tA = CE_TERTIARY(cesA[iA]);
        unsigned char tB = CE_TERTIARY(cesB[iB]);
        if (tA < tB) return -1;
        if (tA > tB) return 1;
        iA++; iB++;
    }
    while (iA < nCEsA && 0 == CE_TERTIARY(cesA[iA])) iA++;
    while (iB < nCEsB && 0 == CE_TERTIARY(cesB[iB])) iB++;
    if (iA < nCEsA) return 1;
    if (iB < nCEsB) return -1;

    /* Tiebreaker: binary comparison of NFC-normalized forms.
     * UCA requires canonical equivalents to compare equal.  Levels 1-3
     * already handle this (CEs are identical for equivalent forms), but
     * the tiebreaker must also use normalized bytes so that decomposed
     * and precomposed forms don't diverge here.
     *
     * We defer normalization to this point because most comparisons
     * resolve at levels 1-3 and never reach the tiebreaker.
     */
    unsigned char nfcBufA[UTF_BUFSIZE], nfcBufB[UTF_BUFSIZE];
    const unsigned char *tieA = a;
    const unsigned char *tieB = b;
    size_t tieNA = nA, tieNB = nB;

    if (!utf_nfc_is_nfc(a, nA)) {
        utf_nfc_normalize(a, nA, nfcBufA, sizeof(nfcBufA), &tieNA);
        tieA = nfcBufA;
    }
    if (!utf_nfc_is_nfc(b, nB)) {
        utf_nfc_normalize(b, nB, nfcBufB, sizeof(nfcBufB), &tieNB);
        tieB = nfcBufB;
    }

    size_t nMin = (tieNA < tieNB) ? tieNA : tieNB;
    int cmp = memcmp(tieA, tieB, nMin);
    if (0 != cmp) return cmp;
    if (tieNA < tieNB) return -1;
    if (tieNA > tieNB) return 1;
    return 0;
}

int utf_collate_cmp_ci(const unsigned char *a, size_t nA,
                       const unsigned char *b, size_t nB)
{
    uint32_t cesA[MAX_CMP_CES], cesB[MAX_CMP_CES];
    int nCEsA = CollectCEs(a, nA, cesA, MAX_CMP_CES);
    int nCEsB = CollectCEs(b, nB, cesB, MAX_CMP_CES);
    int iA, iB;

    /* Level 1: primary. */
    iA = 0; iB = 0;
    for (;;) {
        while (iA < nCEsA && 0 == CE_PRIMARY(cesA[iA])) iA++;
        while (iB < nCEsB && 0 == CE_PRIMARY(cesB[iB])) iB++;
        if (iA >= nCEsA || iB >= nCEsB) break;
        unsigned short pA = CE_PRIMARY(cesA[iA]);
        unsigned short pB = CE_PRIMARY(cesB[iB]);
        if (pA < pB) return -1;
        if (pA > pB) return 1;
        iA++; iB++;
    }
    while (iA < nCEsA && 0 == CE_PRIMARY(cesA[iA])) iA++;
    while (iB < nCEsB && 0 == CE_PRIMARY(cesB[iB])) iB++;
    if (iA < nCEsA) return 1;
    if (iB < nCEsB) return -1;

    /* Level 2: secondary. */
    iA = 0; iB = 0;
    for (;;) {
        while (iA < nCEsA && 0 == CE_SECONDARY(cesA[iA])) iA++;
        while (iB < nCEsB && 0 == CE_SECONDARY(cesB[iB])) iB++;
        if (iA >= nCEsA || iB >= nCEsB) break;
        unsigned short sA = CE_SECONDARY(cesA[iA]);
        unsigned short sB = CE_SECONDARY(cesB[iB]);
        if (sA < sB) return -1;
        if (sA > sB) return 1;
        iA++; iB++;
    }
    while (iA < nCEsA && 0 == CE_SECONDARY(cesA[iA])) iA++;
    while (iB < nCEsB && 0 == CE_SECONDARY(cesB[iB])) iB++;
    if (iA < nCEsA) return 1;
    if (iB < nCEsB) return -1;

    return 0;
}

size_t utf_collate_sortkey(const unsigned char *src, size_t nSrc,
                           unsigned char *key, size_t nKeyMax)
{
    uint32_t ces[MAX_SORTKEY_CES];
    int nCEs = CollectCEs(src, nSrc, ces, MAX_SORTKEY_CES);
    size_t pos = 0;

    /* Level 1: primary (16-bit big-endian). */
    for (int i = 0; i < nCEs; i++) {
        unsigned short p = CE_PRIMARY(ces[i]);
        if (0 != p && pos + 2 <= nKeyMax) {
            key[pos++] = (unsigned char)(p >> 8);
            key[pos++] = (unsigned char)(p & 0xFF);
        }
    }
    if (pos + 2 <= nKeyMax) { key[pos++] = 0; key[pos++] = 0; }

    /* Level 2: secondary (16-bit big-endian). */
    for (int i = 0; i < nCEs; i++) {
        unsigned short s = CE_SECONDARY(ces[i]);
        if (0 != s && pos + 2 <= nKeyMax) {
            key[pos++] = (unsigned char)(s >> 8);
            key[pos++] = (unsigned char)(s & 0xFF);
        }
    }
    if (pos + 2 <= nKeyMax) { key[pos++] = 0; key[pos++] = 0; }

    /* Level 3: tertiary (8-bit). */
    for (int i = 0; i < nCEs; i++) {
        unsigned char t = CE_TERTIARY(ces[i]);
        if (0 != t && pos + 1 <= nKeyMax)
            key[pos++] = t;
    }
    return pos;
}

size_t utf_collate_sortkey_ci(const unsigned char *src, size_t nSrc,
                              unsigned char *key, size_t nKeyMax)
{
    uint32_t ces[MAX_SORTKEY_CES];
    int nCEs = CollectCEs(src, nSrc, ces, MAX_SORTKEY_CES);
    size_t pos = 0;

    for (int i = 0; i < nCEs; i++) {
        unsigned short p = CE_PRIMARY(ces[i]);
        if (0 != p && pos + 2 <= nKeyMax) {
            key[pos++] = (unsigned char)(p >> 8);
            key[pos++] = (unsigned char)(p & 0xFF);
        }
    }
    if (pos + 2 <= nKeyMax) { key[pos++] = 0; key[pos++] = 0; }

    for (int i = 0; i < nCEs; i++) {
        unsigned short s = CE_SECONDARY(ces[i]);
        if (0 != s && pos + 2 <= nKeyMax) {
            key[pos++] = (unsigned char)(s >> 8);
            key[pos++] = (unsigned char)(s & 0xFF);
        }
    }
    return pos;
}
