/*
 * cie97.c — CIE97 perceptual color distance with Kd-tree nearest-color search.
 *
 * Ported from TinyMUX mux/lib/stringutil.cpp (C++) to pure C.
 *
 * Pipeline: sRGB (0-255) -> linear RGB (0-1) -> XYZ (D65) -> CIELAB
 * Distance: CIE76 (squared Euclidean in CIELAB)
 * Search:   Kd-tree cycling through L*, a*, b* axes
 */

#include "utf/cie97.h"
#include <math.h>
#include <stdint.h>

/* sRGB gamma decode: sRGB byte -> linear [0,1]. */
static double srgb_to_linear(int c)
{
    double v = c / 255.0;
    return (v <= 0.04045) ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

/* CIE L*a*b* nonlinear compression. */
static double cie_f(double t)
{
    return (t > 0.008856) ? cbrt(t) : 7.787 * t + 16.0 / 116.0;
}

/* Convert sRGB (0-255) to integer-scaled CIELAB (D65 illuminant).
 * Output: L*100, a*100, b*100.
 */
void utf_rgb2lab(RGB *rgb, LABi *labi)
{
    double lr = srgb_to_linear(rgb->r);
    double lg = srgb_to_linear(rgb->g);
    double lb = srgb_to_linear(rgb->b);

    double x = (lr * 0.4124564 + lg * 0.3575761 + lb * 0.1804375) / 0.95047;
    double y = (lr * 0.2126729 + lg * 0.7151522 + lb * 0.0721750) / 1.00000;
    double z = (lr * 0.0193339 + lg * 0.1191920 + lb * 0.9503041) / 1.08883;

    double fx = cie_f(x), fy = cie_f(y), fz = cie_f(z);

    double L = 116.0 * fy - 16.0;
    double a = 500.0 * (fx - fy);
    double b = 200.0 * (fy - fz);

    labi->L = (int)(L * 100.0 + 0.5);
    labi->a = (int)(a * 100.0 + (a >= 0 ? 0.5 : -0.5));
    labi->b = (int)(b * 100.0 + (b >= 0 ? 0.5 : -0.5));
}

/* CIE76 color difference: squared Euclidean distance in CIELAB. */
static int64_t lab_diff(const LABi *lab1, const LABi *lab2)
{
    int64_t dL = (int64_t)(lab1->L - lab2->L);
    int64_t da = (int64_t)(lab1->a - lab2->a);
    int64_t db = (int64_t)(lab1->b - lab2->b);
    return dL*dL + da*da + db*db;
}

/* Forward declarations for mutually-recursive Kd-tree traversal. */
static void nearest_tree_a(int iHere, const LABi *labi, int *iBest, int64_t *rBest);
static void nearest_tree_b(int iHere, const LABi *labi, int *iBest, int64_t *rBest);
static void nearest_tree_L(int iHere, const LABi *labi, int *iBest, int64_t *rBest);

static void nearest_tree_L(int iHere, const LABi *labi, int *iBest, int64_t *rBest)
{
    if (-1 == iHere) return;

    if (-1 == *iBest)
    {
        *iBest = iHere;
        *rBest = lab_diff(labi, &palette[*iBest].labi);
    }

    int64_t rHere = lab_diff(labi, &palette[iHere].labi);
    if (rHere < *rBest)
    {
        *iBest = iHere;
        *rBest = rHere;
    }

    int64_t d = (int64_t)(labi->L - palette[iHere].labi.L);
    int iNearChild = (d < 0) ? 0 : 1;
    nearest_tree_a(palette[iHere].child[iNearChild], labi, iBest, rBest);

    int64_t rAxis = d * d;
    if (rAxis < *rBest)
    {
        nearest_tree_a(palette[iHere].child[1-iNearChild], labi, iBest, rBest);
    }
}

static void nearest_tree_a(int iHere, const LABi *labi, int *iBest, int64_t *rBest)
{
    if (-1 == iHere) return;

    if (-1 == *iBest)
    {
        *iBest = iHere;
        *rBest = lab_diff(labi, &palette[*iBest].labi);
    }

    int64_t rHere = lab_diff(labi, &palette[iHere].labi);
    if (rHere < *rBest)
    {
        *iBest = iHere;
        *rBest = rHere;
    }

    int64_t d = (int64_t)(labi->a - palette[iHere].labi.a);
    int iNearChild = (d < 0) ? 0 : 1;
    nearest_tree_b(palette[iHere].child[iNearChild], labi, iBest, rBest);

    int64_t rAxis = d * d;
    if (rAxis < *rBest)
    {
        nearest_tree_b(palette[iHere].child[1-iNearChild], labi, iBest, rBest);
    }
}

static void nearest_tree_b(int iHere, const LABi *labi, int *iBest, int64_t *rBest)
{
    if (-1 == iHere) return;

    if (-1 == *iBest)
    {
        *iBest = iHere;
        *rBest = lab_diff(labi, &palette[*iBest].labi);
    }

    int64_t rHere = lab_diff(labi, &palette[iHere].labi);
    if (rHere < *rBest)
    {
        *iBest = iHere;
        *rBest = rHere;
    }

    int64_t d = (int64_t)(labi->b - palette[iHere].labi.b);
    int iNearChild = (d < 0) ? 0 : 1;
    nearest_tree_L(palette[iHere].child[iNearChild], labi, iBest, rBest);

    int64_t rAxis = d * d;
    if (rAxis < *rBest)
    {
        nearest_tree_L(palette[iHere].child[1-iNearChild], labi, iBest, rBest);
    }
}

static int FindNearestPaletteEntry(RGB *rgb, int fColor256)
{
    LABi labi;
    utf_rgb2lab(rgb, &labi);

    int64_t d;
    int j = -1;
    nearest_tree_L(fColor256 ? PALETTE256_ROOT : PALETTE16_ROOT, &labi, &j, &d);
    return j;
}

int co_nearest_xterm256(const unsigned char *rgb)
{
    RGB c;
    c.r = rgb[0];
    c.g = rgb[1];
    c.b = rgb[2];
    return FindNearestPaletteEntry(&c, 1);
}

int co_nearest_xterm16(const unsigned char *rgb)
{
    RGB c;
    c.r = rgb[0];
    c.g = rgb[1];
    c.b = rgb[2];
    return FindNearestPaletteEntry(&c, 0);
}
