/*
 * classify.h — Unicode character classification for UTF-8.
 *
 * Membership tests driven by a compressed DFA over raw UTF-8 bytes, in the
 * same style as the rest of the library: one byte in, one state transition,
 * no intermediate code point decoding.
 *
 * Unicode 16.0.  Locale-independent.
 */

#ifndef UTF_CLASSIFY_H
#define UTF_CLASSIFY_H

#include "utf_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * utf_is_word — Is the code point at [p, pEnd) a word character?
 *
 * The set is Unicode's word-ish core:
 *
 *   Alphabetic  the derived property, which already folds in Lu/Ll/Lt/Lm/Lo,
 *               Nl, and Other_Alphabetic (so circled letters such as U+24B6
 *               count, which a bare general-category test would miss)
 *   Nd          decimal digits
 *   Mn, Mc      non-spacing and spacing combining marks, so that a base plus
 *               its accents is one word
 *
 * Pc (connector punctuation, where '_' lives) is deliberately NOT included;
 * see utf_is_word_connector().
 *
 * Returns 1 for a word character, 0 otherwise.  Returns 0 for an empty or
 * malformed sequence — this answers a question about text, and invalid input
 * is not a word character.
 *
 * pEnd bounds the read.  Pass the end of the code point (or of the buffer);
 * the DFA stops as soon as it can decide, so a pEnd past the code point is
 * harmless.
 */
UTF_API int utf_is_word(const unsigned char *p, const unsigned char *pEnd);

/*
 * utf_is_word_connector — Is the code point at [p, pEnd) in Pc?
 *
 * The ten connector punctuation code points: U+005F LOW LINE, U+203F..U+2040,
 * U+2054, U+FE33..U+FE34, U+FE4D..U+FE4F, U+FF3F.
 *
 * Exposed separately from utf_is_word because a caller implementing an
 * intraword-underscore rule (Pandoc leaves '_' alone inside a word and treats
 * it as emphasis between words) is deciding ABOUT the underscore, and needs
 * it not to already count as part of the word.  Callers who want the union
 * can take it.
 *
 * Returns 1 for connector punctuation, 0 otherwise.
 */
UTF_API int utf_is_word_connector(const unsigned char *p,
                                  const unsigned char *pEnd);

#ifdef __cplusplus
}
#endif

#endif /* UTF_CLASSIFY_H */
