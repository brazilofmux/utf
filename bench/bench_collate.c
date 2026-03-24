/*
 * bench_collate.c — DUCET collation benchmark: libutf vs ICU.
 *
 * Two scenarios:
 *   1. UTF-8 input (libutf's native format)
 *   2. UTF-16 input (ICU's native format)
 *
 * Build:
 *   gcc -O2 -I../include -o bench_collate bench_collate.c -L.. -lutf -lm \
 *       $(pkg-config --cflags --libs icu-uc icu-i18n)
 *
 * Usage: ./bench_collate [iterations]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "utf/collate.h"

/* ICU headers */
#include <unicode/ucol.h>
#include <unicode/ustring.h>
#include <unicode/utypes.h>

/* Pairs of strings to compare. */
static const char *pairs[][2] = {
    { "apple",  "banana" },
    { "café",   "cafe" },
    { "Café",   "café" },
    { "naïve",  "naive" },
    { "Ångström", "angstrom" },
    { "straße", "strasse" },
    { "\xc3\xa9", "e" },  /* é vs e */
    { "\xe4\xb8\xad\xe6\x96\x87", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e" }, /* 中文 vs 日本語 */
    { "abc",    "abc" },
    { "The quick brown fox", "The quick brown fox jumps" },
    { "\xed\x95\x9c\xea\xb5\xad", "\xec\xa4\x91\xea\xb5\xad" }, /* 한국 vs 중국 */
    { "resume", "r\x65\xcc\x81sum\x65\xcc\x81" }, /* resume vs résumé (decomposed) */
};
static const int nPairs = sizeof(pairs) / sizeof(pairs[0]);

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    int iterations = (argc > 1) ? atoi(argv[1]) : 100000;
    volatile int sink = 0;

    UErrorCode err = U_ZERO_ERROR;
    UCollator *coll = ucol_open("", &err);
    if (U_FAILURE(err)) {
        fprintf(stderr, "ICU error: %s\n", u_errorName(err));
        return 1;
    }

    /* Pre-convert pairs to UTF-16 for Scenario 2. */
    UChar u16a[12][512], u16b[12][512];
    int32_t u16a_len[12], u16b_len[12];
    size_t utf8a_len[12], utf8b_len[12];
    for (int i = 0; i < nPairs; i++) {
        utf8a_len[i] = strlen(pairs[i][0]);
        utf8b_len[i] = strlen(pairs[i][1]);
        u_strFromUTF8(u16a[i], 512, &u16a_len[i],
                      pairs[i][0], (int32_t)utf8a_len[i], &err);
        u_strFromUTF8(u16b[i], 512, &u16b_len[i],
                      pairs[i][1], (int32_t)utf8b_len[i], &err);
    }

    printf("DUCET Collation Benchmark\n");
    printf("Iterations: %d per pair, %d pairs\n", iterations, nPairs);

    /* ================================================================
     * Scenario 1: UTF-8 input (libutf native, ICU pays for conversion)
     * ================================================================ */
    printf("\n--- Scenario 1: UTF-8 input ---\n");

    /* libutf: native UTF-8. */
    double t0 = now_sec();
    for (int iter = 0; iter < iterations; iter++) {
        for (int i = 0; i < nPairs; i++) {
            sink += utf_collate_cmp(
                (const unsigned char *)pairs[i][0], utf8a_len[i],
                (const unsigned char *)pairs[i][1], utf8b_len[i]);
        }
    }
    double t1 = now_sec();
    double libutf_utf8 = (t1 - t0) * 1000.0;

    /* ICU: UTF-8 -> UTF-16 -> collate. */
    t0 = now_sec();
    for (int iter = 0; iter < iterations; iter++) {
        for (int i = 0; i < nPairs; i++) {
            UChar ua[512], ub[512];
            int32_t ual, ubl;
            u_strFromUTF8(ua, 512, &ual, pairs[i][0],
                          (int32_t)utf8a_len[i], &err);
            u_strFromUTF8(ub, 512, &ubl, pairs[i][1],
                          (int32_t)utf8b_len[i], &err);
            sink += ucol_strcoll(coll, ua, ual, ub, ubl);
        }
    }
    t1 = now_sec();
    double icu_utf8 = (t1 - t0) * 1000.0;

    printf("  %-24s %10.1f ms\n", "libutf (native)", libutf_utf8);
    printf("  %-24s %10.1f ms\n", "ICU (convert+collate)", icu_utf8);
    if (icu_utf8 >= libutf_utf8)
        printf("  %-24s %10.1fx faster\n", "libutf", icu_utf8 / libutf_utf8);
    else
        printf("  %-24s %10.1fx faster\n", "ICU", libutf_utf8 / icu_utf8);

    /* ================================================================
     * Scenario 2: UTF-16 input (ICU native, libutf pays for conversion)
     * ================================================================ */
    printf("\n--- Scenario 2: UTF-16 input ---\n");

    /* ICU: native UTF-16. */
    t0 = now_sec();
    for (int iter = 0; iter < iterations; iter++) {
        for (int i = 0; i < nPairs; i++) {
            sink += ucol_strcoll(coll, u16a[i], u16a_len[i],
                                      u16b[i], u16b_len[i]);
        }
    }
    t1 = now_sec();
    double icu_utf16 = (t1 - t0) * 1000.0;

    /* libutf: UTF-16 -> UTF-8 -> collate. */
    unsigned char buf8a[512], buf8b[512];
    t0 = now_sec();
    for (int iter = 0; iter < iterations; iter++) {
        for (int i = 0; i < nPairs; i++) {
            int32_t u8alen, u8blen;
            u_strToUTF8((char *)buf8a, 512, &u8alen,
                        u16a[i], u16a_len[i], &err);
            u_strToUTF8((char *)buf8b, 512, &u8blen,
                        u16b[i], u16b_len[i], &err);
            sink += utf_collate_cmp(buf8a, (size_t)u8alen,
                                    buf8b, (size_t)u8blen);
        }
    }
    t1 = now_sec();
    double libutf_utf16 = (t1 - t0) * 1000.0;

    printf("  %-24s %10.1f ms\n", "ICU (native)", icu_utf16);
    printf("  %-24s %10.1f ms\n", "libutf (convert+collate)", libutf_utf16);
    if (icu_utf16 >= libutf_utf16)
        printf("  %-24s %10.1fx faster\n", "libutf", icu_utf16 / libutf_utf16);
    else
        printf("  %-24s %10.1fx faster\n", "ICU", libutf_utf16 / icu_utf16);

    printf("\n--- Summary ---\n");
    printf("  libutf collate (UTF-8 native):    %8.1f ms\n", libutf_utf8);
    printf("  ICU collate (UTF-16 native):      %8.1f ms\n", icu_utf16);
    if (icu_utf16 >= libutf_utf8)
        printf("  Core-to-core:                      libutf %.1fx faster\n",
               icu_utf16 / libutf_utf8);
    else
        printf("  Core-to-core:                      ICU %.1fx faster\n",
               libutf_utf8 / icu_utf16);

    ucol_close(coll);
    (void)sink;
    return 0;
}
