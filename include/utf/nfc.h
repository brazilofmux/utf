/*
 * nfc.h — Unicode NFC normalization for UTF-8 strings.
 *
 * Implements NFC (Canonical Decomposition followed by Canonical Composition)
 * using DFA state machines for all Unicode property lookups.  Hangul
 * composition and decomposition are handled algorithmically.
 *
 * Unicode 16.0.  All operations are locale-independent.
 */

#ifndef UTF_NFC_H
#define UTF_NFC_H

#include "utf_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * utf_nfc_is_nfc — Quick check whether a UTF-8 string is already in NFC.
 *
 * Returns 1 if the string is definitely in NFC, 0 if it is not (or might
 * not be).  Uses the NFC_QC property and CCC ordering check.
 */
UTF_API int utf_nfc_is_nfc(const unsigned char *src, size_t nSrc);

/*
 * Status returned by utf_nfc_normalize.
 *
 * Anything other than UTF_NFC_OK means the output is SHORTER than the
 * correct answer.  It is always a valid, correctly normalized prefix — the
 * result is never left half-composed or split mid-sequence — but it is a
 * prefix, and *pnDst alone cannot tell you that: NFC legitimately shrinks
 * text, so a short result is not by itself evidence of loss.  Check this.
 */
typedef enum {
    UTF_NFC_OK = 0,

    /* dst was too small.  *pnDst holds the prefix that fit.  Size the
     * buffer with utf_nfc_normalize_bound() and retry. */
    UTF_NFC_TRUNCATED = 1,

    /* A single combining character sequence exceeded the internal segment
     * limit (UTF_NFC_SEG_MAX code points after decomposition, 1024 by
     * default).  Only reachable from pathological input — UAX #15's
     * Stream-Safe Text Format caps a sequence at 30 non-starters — but it
     * is reachable from hostile input, so it is reported rather than
     * silently dropped.  *pnDst holds everything before the offending
     * sequence.  Rebuild with -DUTF_NFC_SEG_MAX=N to raise the limit. */
    UTF_NFC_SEGMENT_TOO_LONG = 2
} utf_nfc_status;

/*
 * utf_nfc_normalize_bound — Destination size that can never truncate.
 *
 * NFC can EXPAND text: a composition exclusion such as U+0958 is 3 bytes
 * in and 6 bytes out, so nSrc bytes of input is not a safe destination
 * size.  UAX #15 bounds NFC expansion at 3x for UTF-8; this returns that
 * bound.  A destination of this size makes UTF_NFC_TRUNCATED impossible.
 */
UTF_API size_t utf_nfc_normalize_bound(size_t nSrc);

/*
 * utf_nfc_normalize — Normalize a UTF-8 string to NFC.
 *
 * src/nSrc:    Input UTF-8 string.
 * dst/nDstMax: Output buffer and its capacity.
 * pnDst:       On return, number of bytes written to dst.
 *
 * Clean runs are copied through directly; only combining character
 * sequences that are not already NFC are decomposed, reordered and
 * recomposed.
 *
 * Returns UTF_NFC_OK, or a status describing why the output is a prefix.
 * DO NOT IGNORE THE RETURN VALUE unless nDstMax >= utf_nfc_normalize_bound(nSrc)
 * — see utf_nfc_status.
 */
UTF_API utf_nfc_status utf_nfc_normalize(const unsigned char *src, size_t nSrc,
                                         unsigned char *dst, size_t nDstMax,
                                         size_t *pnDst);

#ifdef __cplusplus
}
#endif

#endif /* UTF_NFC_H */
