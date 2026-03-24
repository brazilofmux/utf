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
 * utf_nfc_normalize — Normalize a UTF-8 string to NFC.
 *
 * src/nSrc:    Input UTF-8 string.
 * dst/nDstMax: Output buffer and its capacity.
 * pnDst:       On return, number of bytes written to dst.
 *
 * If the input is already NFC (per quick check), this is a fast memcpy.
 * Otherwise performs full NFD decomposition, canonical reordering, and
 * canonical composition.
 */
UTF_API void utf_nfc_normalize(const unsigned char *src, size_t nSrc,
                               unsigned char *dst, size_t nDstMax,
                               size_t *pnDst);

#ifdef __cplusplus
}
#endif

#endif /* UTF_NFC_H */
