/*
 * collate.h — Unicode Collation Algorithm (UCA) per UTS #10.
 *
 * DUCET-based string comparison and sort key generation for
 * linguistically correct ordering.  Unicode 16.0.
 *
 * All operations are locale-independent (default DUCET ordering).
 */

#ifndef UTF_COLLATE_H
#define UTF_COLLATE_H

#include "utf_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * utf_collate_cmp — Compare two UTF-8 strings using UCA.
 *
 * Multi-level comparison:
 *   Level 1: primary weights (base character identity)
 *   Level 2: secondary weights (accents)
 *   Level 3: tertiary weights (case)
 *   Tiebreaker: binary code-point order
 *
 * Returns negative if a < b, 0 if equal, positive if a > b.
 */
UTF_API int utf_collate_cmp(const unsigned char *a, size_t nA,
                            const unsigned char *b, size_t nB);

/*
 * utf_collate_cmp_ci — Case-insensitive UCA comparison.
 *
 * Same as utf_collate_cmp but skips Level 3 (tertiary/case).
 */
UTF_API int utf_collate_cmp_ci(const unsigned char *a, size_t nA,
                               const unsigned char *b, size_t nB);

/*
 * utf_collate_sortkey — Generate a binary sort key for UCA comparison.
 *
 * The sort key can be compared with memcmp to get the same ordering
 * as utf_collate_cmp.  Returns bytes written to key.
 */
UTF_API size_t utf_collate_sortkey(const unsigned char *src, size_t nSrc,
                                   unsigned char *key, size_t nKeyMax);

/*
 * utf_collate_sortkey_ci — Case-insensitive sort key generation.
 *
 * Same as utf_collate_sortkey but omits Level 3 (tertiary/case).
 */
UTF_API size_t utf_collate_sortkey_ci(const unsigned char *src, size_t nSrc,
                                      unsigned char *key, size_t nKeyMax);

#ifdef __cplusplus
}
#endif

#endif /* UTF_COLLATE_H */
