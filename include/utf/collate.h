/*
 * collate.h — Unicode Collation Algorithm (UCA) per UTS #10.
 *
 * DUCET-based string comparison and sort key generation for
 * linguistically correct ordering.  Unicode 16.0.
 *
 * Each operation comes in two forms: the plain one uses root collation
 * (DUCET, no tailoring), and the _l form takes a collator, POSIX-style as
 * the last argument, where NULL also means root.  Collators are constant
 * tables built into the library -- utf_collator_find never allocates, and
 * the result needs no freeing and is safe to share between threads.
 *
 * Built in: root plus 53 European locales from CLDR 46 -- az be bg br bs
 * ca cs cy da de de_AT dsb el en eo es et fi fo fr fr_CA fy ga gl hr hsb
 * hu is it kl lb lt lv mk mt nb nl nn no pl pt ro ru se sk sl smn sq sr
 * sr_Latn sv tr uk.  Some (de, en, fr, ...) have no rules of their own and
 * order exactly as root.
 */

#ifndef UTF_COLLATE_H
#define UTF_COLLATE_H

#include "utf_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An opaque, constant collator: one locale's ordering. */
typedef struct utf_collator utf_collator;

/*
 * utf_collator_find — Look up the collator for a locale.
 *
 * Accepts BCP 47 or POSIX spellings ("sv-SE", "sv_SE", "sv_SE.UTF-8"),
 * case-insensitively, and falls back one subtag at a time ("de-AT-1996",
 * "de-AT", "de").  "root", "und", "" and NULL name root collation.
 *
 * Returns NULL if no collator matches -- a locale this build has no
 * ordering for is not quietly given root's.
 */
UTF_API const utf_collator *utf_collator_find(const char *locale);

/* utf_collator_root — The root collator (DUCET, no tailoring). */
UTF_API const utf_collator *utf_collator_root(void);

/* utf_collator_name — The collator's registry name ("root", ...); NULL
 * names root. */
UTF_API const char *utf_collator_name(const utf_collator *c);

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
UTF_API int utf_collate_cmp_l(const unsigned char *a, size_t nA,
                              const unsigned char *b, size_t nB,
                              const utf_collator *c);

/*
 * utf_collate_cmp_ci — Case-insensitive UCA comparison.
 *
 * Same as utf_collate_cmp but skips Level 3 (tertiary/case).
 */
UTF_API int utf_collate_cmp_ci(const unsigned char *a, size_t nA,
                               const unsigned char *b, size_t nB);
UTF_API int utf_collate_cmp_ci_l(const unsigned char *a, size_t nA,
                                 const unsigned char *b, size_t nB,
                                 const utf_collator *c);

/*
 * utf_collate_sortkey — Generate a binary sort key for UCA comparison.
 *
 * The sort key can be compared with memcmp to get the same ordering
 * as utf_collate_cmp.  Returns bytes written to key.  Keys are comparable
 * only with keys from the same collator and the same library build.
 */
UTF_API size_t utf_collate_sortkey(const unsigned char *src, size_t nSrc,
                                   unsigned char *key, size_t nKeyMax);
UTF_API size_t utf_collate_sortkey_l(const unsigned char *src, size_t nSrc,
                                     unsigned char *key, size_t nKeyMax,
                                     const utf_collator *c);

/*
 * utf_collate_sortkey_ci — Case-insensitive sort key generation.
 *
 * Same as utf_collate_sortkey but omits Level 3 (tertiary/case).
 */
UTF_API size_t utf_collate_sortkey_ci(const unsigned char *src, size_t nSrc,
                                      unsigned char *key, size_t nKeyMax);
UTF_API size_t utf_collate_sortkey_ci_l(const unsigned char *src, size_t nSrc,
                                        unsigned char *key, size_t nKeyMax,
                                        const utf_collator *c);

#ifdef __cplusplus
}
#endif

#endif /* UTF_COLLATE_H */
