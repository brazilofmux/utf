/*
 * grapheme.h — Extended Grapheme Cluster segmentation per UAX #29.
 *
 * Standalone API for Unicode 16.0 grapheme cluster boundary detection.
 * Uses DFA-based GCB property lookup and Extended_Pictographic
 * classification.  All operations are locale-independent.
 *
 * For PUA-colored strings, use the co_cluster_* functions in color_ops.h
 * instead — they handle inline color transparently.
 */

#ifndef UTF_GRAPHEME_H
#define UTF_GRAPHEME_H

#include "utf_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * utf_grapheme_next — Advance past one Extended Grapheme Cluster.
 *
 * Given a UTF-8 string [src, src+nSrc), returns the number of bytes
 * consumed by the next grapheme cluster.  Returns 0 if nSrc is 0.
 *
 * Implements all GB rules from UAX #29 (Unicode 16.0):
 *   GB3-5:   CR/LF/Control boundaries
 *   GB6-8:   Hangul jamo sequences
 *   GB9:     Extend and ZWJ
 *   GB9a/b:  SpacingMark and Prepend
 *   GB11:    Emoji ZWJ sequences (ExtPict Extend* ZWJ x ExtPict)
 *   GB12/13: Regional Indicator pairs
 */
UTF_API size_t utf_grapheme_next(const unsigned char *src, size_t nSrc);

/*
 * utf_grapheme_count — Count grapheme clusters in a UTF-8 string.
 */
UTF_API size_t utf_grapheme_count(const unsigned char *src, size_t nSrc);

#ifdef __cplusplus
}
#endif

#endif /* UTF_GRAPHEME_H */
