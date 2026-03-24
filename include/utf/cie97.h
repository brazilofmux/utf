/*
 * cie97.h — CIE97 perceptual color distance with Kd-tree nearest-color search.
 *
 * Provides sRGB -> CIELAB conversion and nearest xterm-256/16 palette
 * lookup using a pre-built Kd-tree over the 256-entry xterm palette.
 * The distance metric is CIE76 (squared Euclidean in CIELAB space),
 * which approximates CIE94/CIE97 for the small-gamut xterm palette.
 */

#ifndef CIE97_H
#define CIE97_H

#include "utf_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int r;
    int g;
    int b;
} RGB;

typedef struct {
    int L;    /* L* scaled by 100 */
    int a;    /* a* scaled by 100 */
    int b;    /* b* scaled by 100 */
} LABi;

typedef struct {
    RGB   rgb;
    LABi  labi;
    int   child[2];
    int   color8;
    int   color16;
} PALETTE_ENTRY;

#define PALETTE16_ROOT  2
#define PALETTE256_ROOT 72

extern UTF_API PALETTE_ENTRY palette[];

/* Convert sRGB (0-255) to integer-scaled CIELAB (D65 illuminant). */
void utf_rgb2lab(RGB *rgb, LABi *labi);

/*
 * Find the nearest xterm-256 palette entry for an RGB color.
 * rgb must point to 3 bytes: [R, G, B], each 0-255.
 * Returns the xterm-256 palette index (0-255).
 */
UTF_API int co_nearest_xterm256(const unsigned char *rgb);

/*
 * Find the nearest xterm-16 (ANSI) palette entry.
 * Returns the palette index (0-15).
 */
UTF_API int co_nearest_xterm16(const unsigned char *rgb);

#ifdef __cplusplus
}
#endif

#endif /* CIE97_H */
