/*
 * grapheme.c — Extended Grapheme Cluster segmentation per UAX #29.
 *
 * Standalone API for grapheme cluster boundary detection on plain
 * (non-PUA) UTF-8 strings.  Uses the same DFA-based algorithm as
 * the co_cluster_* functions in color_ops.c.
 *
 * Unicode 16.0.
 */

#include "utf/grapheme.h"
#include "utf/utf_tables.h"

/* GCB property values (must match gen_gcb.pl mapping). */
enum {
    GCB_Other              = 0,
    GCB_CR                 = 1,
    GCB_LF                 = 2,
    GCB_Control            = 3,
    GCB_Extend             = 4,
    GCB_ZWJ                = 5,
    GCB_Regional_Indicator = 6,
    GCB_Prepend            = 7,
    GCB_SpacingMark        = 8,
    GCB_L                  = 9,
    GCB_V                  = 10,
    GCB_T                  = 11,
    GCB_LV                 = 12,
    GCB_LVT                = 13
};

/* Run an integer-result DFA on a byte sequence. */
static int run_dfa(const unsigned char *itt, const unsigned short *sot,
                   const unsigned char *sbt, int start, int accept_start,
                   int default_val, const unsigned char *p,
                   const unsigned char *pEnd)
{
    int iState = start;
    while (p < pEnd) {
        int iCol = itt[*p++];
        int iOff = sot[iState];
        for (;;) {
            int y = (signed char)sbt[iOff];
            if (y > 0) {
                if (iCol < y) { iState = sbt[iOff + 1]; break; }
                iCol -= y;
                iOff += 2;
            } else {
                y = -y;
                if (iCol < y) { iState = sbt[iOff + iCol + 1]; break; }
                iCol -= y;
                iOff += y + 1;
            }
        }
    }
    return (iState >= accept_start) ? iState - accept_start : default_val;
}

static int gcb_get(const unsigned char *p, const unsigned char *pEnd)
{
    return run_dfa(tr_gcb_itt, tr_gcb_sot, tr_gcb_sbt,
                   TR_GCB_START_STATE, TR_GCB_ACCEPTING_STATES_START,
                   GCB_Other, p, pEnd);
}

static int gcb_is_extpict(const unsigned char *p, const unsigned char *pEnd)
{
    int r = run_dfa(cl_extpict_itt, cl_extpict_sot, cl_extpict_sbt,
                    CL_EXTPICT_START_STATE, CL_EXTPICT_ACCEPTING_STATES_START,
                    0, p, pEnd);
    return (r == 1);
}

/* Advance one UTF-8 code point.  Returns pointer past the code point. */
static const unsigned char *utf8_cp_advance(const unsigned char *p,
                                            const unsigned char *pEnd)
{
    if (p >= pEnd) return p;
    unsigned char ch = *p;
    size_t n;
    if (ch < 0x80)      n = 1;
    else if (ch < 0xE0) n = 2;
    else if (ch < 0xF0) n = 3;
    else                 n = 4;
    return (p + n <= pEnd) ? p + n : pEnd;
}

size_t utf_grapheme_next(const unsigned char *src, size_t nSrc)
{
    const unsigned char *p = src;
    const unsigned char *pEnd = src + nSrc;

    if (p >= pEnd) return 0;

    /* First code point. */
    const unsigned char *pFirstEnd = utf8_cp_advance(p, pEnd);
    int prevGCB = gcb_get(p, pFirstEnd);
    int bPrevExtPict = gcb_is_extpict(p, pFirstEnd);
    const unsigned char *pCur = pFirstEnd;

    /* GB4: (Control|CR|LF) ÷ */
    if (GCB_Control == prevGCB || GCB_LF == prevGCB)
        return (size_t)(pCur - src);

    int bSeenEPEZ = bPrevExtPict;  /* GB11 */
    int nRI = (GCB_Regional_Indicator == prevGCB) ? 1 : 0;  /* GB12/13 */

    while (pCur < pEnd) {
        const unsigned char *pNextEnd = utf8_cp_advance(pCur, pEnd);
        int curGCB = gcb_get(pCur, pNextEnd);
        int bCurExtPict = gcb_is_extpict(pCur, pNextEnd);

        /* GB3: CR × LF */
        if (GCB_CR == prevGCB && GCB_LF == curGCB)
            return (size_t)(pNextEnd - src);

        /* GB5: ÷ (Control|CR|LF) */
        if (GCB_Control == curGCB || GCB_CR == curGCB || GCB_LF == curGCB)
            break;

        int extend = 0;

        /* GB6: L × (L|V|LV|LVT) */
        if (GCB_L == prevGCB &&
            (GCB_L == curGCB || GCB_V == curGCB ||
             GCB_LV == curGCB || GCB_LVT == curGCB))
            extend = 1;

        /* GB7: (LV|V) × (V|T) */
        if (!extend && (GCB_LV == prevGCB || GCB_V == prevGCB) &&
            (GCB_V == curGCB || GCB_T == curGCB))
            extend = 1;

        /* GB8: (LVT|T) × T */
        if (!extend && (GCB_LVT == prevGCB || GCB_T == prevGCB) &&
            GCB_T == curGCB)
            extend = 1;

        /* GB9: × (Extend|ZWJ) */
        if (!extend && (GCB_Extend == curGCB || GCB_ZWJ == curGCB))
            extend = 1;

        /* GB9a: × SpacingMark */
        if (!extend && GCB_SpacingMark == curGCB)
            extend = 1;

        /* GB9b: Prepend × */
        if (!extend && GCB_Prepend == prevGCB)
            extend = 1;

        /* GB11: ExtPict Extend* ZWJ × ExtPict */
        if (!extend && bSeenEPEZ && GCB_ZWJ == prevGCB && bCurExtPict)
            extend = 1;

        /* GB12/13: RI × RI (pairs only) */
        if (!extend && GCB_Regional_Indicator == curGCB && (nRI % 2) == 1)
            extend = 1;

        if (!extend) break;  /* GB999: ÷ */

        /* Continue cluster. */
        if (GCB_Regional_Indicator == curGCB) nRI++;
        if (bCurExtPict) bSeenEPEZ = 1;
        else if (!bSeenEPEZ || (GCB_Extend != curGCB && GCB_ZWJ != curGCB))
            bSeenEPEZ = 0;

        prevGCB = curGCB;
        pCur = pNextEnd;
    }
    return (size_t)(pCur - src);
}

size_t utf_grapheme_count(const unsigned char *src, size_t nSrc)
{
    size_t nClusters = 0;
    size_t nConsumed = 0;

    while (nConsumed < nSrc)
    {
        size_t cb = utf_grapheme_next(src + nConsumed, nSrc - nConsumed);
        if (0 == cb) break;
        nConsumed += cb;
        nClusters++;
    }
    return nClusters;
}
