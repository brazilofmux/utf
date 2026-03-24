/*
 * bench_collate.c — DUCET collation benchmark: libutf vs ICU.
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
    volatile int sink = 0;  /* Prevent optimization. */

    printf("DUCET Collation Benchmark\n");
    printf("Iterations: %d per pair, %d pairs\n\n", iterations, nPairs);

    /* --- libutf --- */
    double t0 = now_sec();
    for (int iter = 0; iter < iterations; iter++) {
        for (int i = 0; i < nPairs; i++) {
            const unsigned char *a = (const unsigned char *)pairs[i][0];
            const unsigned char *b = (const unsigned char *)pairs[i][1];
            sink += utf_collate_cmp(a, strlen(pairs[i][0]), b, strlen(pairs[i][1]));
        }
    }
    double t1 = now_sec();
    double libutf_ms = (t1 - t0) * 1000.0;

    /* --- ICU --- */
    UErrorCode err = U_ZERO_ERROR;
    UCollator *coll = ucol_open("", &err);  /* Root collator = DUCET. */
    if (U_FAILURE(err)) {
        fprintf(stderr, "ICU error: %s\n", u_errorName(err));
        return 1;
    }

    t0 = now_sec();
    for (int iter = 0; iter < iterations; iter++) {
        for (int i = 0; i < nPairs; i++) {
            UChar ua[512], ub[512];
            int32_t uaLen, ubLen;
            u_strFromUTF8(ua, 512, &uaLen, pairs[i][0],
                          (int32_t)strlen(pairs[i][0]), &err);
            u_strFromUTF8(ub, 512, &ubLen, pairs[i][1],
                          (int32_t)strlen(pairs[i][1]), &err);
            sink += ucol_strcoll(coll, ua, uaLen, ub, ubLen);
        }
    }
    t1 = now_sec();
    double icu_ms = (t1 - t0) * 1000.0;

    ucol_close(coll);

    printf("%-20s %10.1f ms\n", "libutf", libutf_ms);
    printf("%-20s %10.1f ms\n", "ICU 74.2", icu_ms);
    printf("%-20s %10.1fx\n", "Speedup", icu_ms / libutf_ms);

    (void)sink;
    return 0;
}
