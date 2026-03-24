/*
 * utf_types.h — Core types and constants for the UTF library.
 *
 * Provides UTF-8/UTF-32 types, string descriptors, and buffer size
 * defaults.  No external dependencies beyond <stddef.h> and <stdint.h>.
 */

#ifndef UTF_TYPES_H
#define UTF_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* Portable API visibility macro. */
#ifndef UTF_API
#if defined(_WIN32) || defined(WIN32)
#ifdef BUILDING_UTF
#define UTF_API __declspec(dllexport)
#else
#define UTF_API __declspec(dllimport)
#endif
#else
#define UTF_API
#endif
#endif

/* Default buffer size for internal scratch and output buffers.
 * Override at compile time with -DUTF_BUFSIZE=N if needed.
 */
#ifndef UTF_BUFSIZE
#define UTF_BUFSIZE 8000
#endif

/* Legacy alias — code ported from TinyMUX may reference LBUF_SIZE. */
#ifndef LBUF_SIZE
#define LBUF_SIZE UTF_BUFSIZE
#endif

/* UTF-8 lead byte classification: sequence length (1-4), 5=continuation, 6=illegal. */
#define CO_UTF8_SIZE1     1
#define CO_UTF8_SIZE2     2
#define CO_UTF8_SIZE3     3
#define CO_UTF8_SIZE4     4
#define CO_UTF8_CONTINUE  5
#define CO_UTF8_ILLEGAL   6

#ifdef __cplusplus
extern "C" {
#endif

/* UTF-8 lead byte -> sequence length lookup table. */
extern UTF_API const unsigned char utf8_FirstByte[256];

#ifdef __cplusplus
}
#endif

/* Replacement string descriptor returned by DFA case-mapping transforms.
 * n_bytes:  length of the replacement UTF-8 string.
 * n_points: number of code points in the replacement.
 * p:        pointer to the replacement bytes (static data).
 */
typedef struct {
    size_t n_bytes;
    size_t n_points;
    const unsigned char *p;
} co_string_desc;

#endif /* UTF_TYPES_H */
