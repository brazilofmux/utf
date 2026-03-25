/*
 * bench_grapheme.c — Grapheme cluster segmentation: libutf vs ICU.
 *
 * Build:
 *   gcc -O2 -I../include -o bench_grapheme bench_grapheme.c -L.. -lutf -lm \
 *       $(pkg-config --cflags --libs icu-uc)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "utf/grapheme.h"

#include <unicode/ubrk.h>
#include <unicode/ustring.h>
#include <unicode/utypes.h>

static const char *test_strings[] = {
    "Hello, World!",
    "caf\xc3\xa9 cr\xc3\xa8me",
    /* Emoji ZWJ sequence: family (man + ZWJ + woman + ZWJ + girl) */
    "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7",
    /* Regional indicators: flag US */
    "\xf0\x9f\x87\xba\xf0\x9f\x87\xb8",
    /* Hangul syllables */
    "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4\xeb\xa5\xbc \xeb\xb0\xb0\xec\x9a\xb0\xea\xb3\xa0 \xec\x9e\x88\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4",
    /* Devanagari with combining marks */
    "\xe0\xa4\xa8\xe0\xa4\xae\xe0\xa4\xb8\xe0\xa5\x8d\xe0\xa4\xa4\xe0\xa5\x87",
    /* Plain ASCII */
    "The quick brown fox jumps over the lazy dog and keeps running forever.",
    /* Mixed CJK + Latin */
    "\xe4\xb8\xad\xe6\x96\x87 mixed with English text \xe5\x92\x8c\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e",
};
static const int nStrings = sizeof(test_strings) / sizeof(test_strings[0]);

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    int iterations = (argc > 1) ? atoi(argv[1]) : 200000;
    volatile size_t sink = 0;

    printf("Grapheme Cluster Segmentation Benchmark\n");
    printf("Iterations: %d per string, %d strings\n\n", iterations, nStrings);

    /* Pre-convert to UTF-16 and cache lengths. */
    UErrorCode err = U_ZERO_ERROR;
    UChar u16[8][1024];
    int32_t u16_len[8];
    size_t utf8_len[8];
    for (int s = 0; s < nStrings; s++) {
        utf8_len[s] = strlen(test_strings[s]);
        u_strFromUTF8(u16[s], 1024, &u16_len[s],
                      test_strings[s], (int32_t)utf8_len[s], &err);
    }

    /* Create a reusable break iterator. */
    UBreakIterator *bi = ubrk_open(UBRK_CHARACTER, "", u16[0], u16_len[0], &err);
    if (U_FAILURE(err)) {
        fprintf(stderr, "ICU error: %s\n", u_errorName(err));
        return 1;
    }

    /* --- Scenario 1: UTF-8 input (libutf native, ICU pays for conversion) --- */
    printf("--- Scenario 1: UTF-8 input ---\n");

    /* libutf: native UTF-8. */
    double t0 = now_sec();
    for (int iter = 0; iter < iterations; iter++) {
        for (int s = 0; s < nStrings; s++) {
            sink += utf_grapheme_count((const unsigned char *)test_strings[s],
                                       utf8_len[s]);
        }
    }
    double t1 = now_sec();
    double libutf_ms = (t1 - t0) * 1000.0;

    /* ICU: UTF-8 -> UTF-16 -> segment. */
    t0 = now_sec();
    for (int iter = 0; iter < iterations; iter++) {
        for (int s = 0; s < nStrings; s++) {
            UChar ustr[1024];
            int32_t ulen;
            u_strFromUTF8(ustr, 1024, &ulen,
                          test_strings[s], (int32_t)utf8_len[s], &err);
            ubrk_setText(bi, ustr, ulen, &err);
            size_t count = 0;
            while (ubrk_next(bi) != UBRK_DONE) count++;
            sink += count;
        }
    }
    t1 = now_sec();
    double icu_utf8_ms = (t1 - t0) * 1000.0;

    printf("  %-24s %10.1f ms\n", "libutf (native)", libutf_ms);
    printf("  %-24s %10.1f ms\n", "ICU (convert+segment)", icu_utf8_ms);
    if (icu_utf8_ms >= libutf_ms)
        printf("  %-24s %10.1fx faster\n", "libutf", icu_utf8_ms / libutf_ms);
    else
        printf("  %-24s %10.1fx faster\n", "ICU", libutf_ms / icu_utf8_ms);

    /* --- Scenario 2: core-to-core (each library in its native encoding) --- */
    printf("\n--- Scenario 2: core-to-core ---\n");

    /* ICU: native UTF-16, reused iterator. */
    t0 = now_sec();
    for (int iter = 0; iter < iterations; iter++) {
        for (int s = 0; s < nStrings; s++) {
            ubrk_setText(bi, u16[s], u16_len[s], &err);
            size_t count = 0;
            while (ubrk_next(bi) != UBRK_DONE) count++;
            sink += count;
        }
    }
    t1 = now_sec();
    double icu_native_ms = (t1 - t0) * 1000.0;

    printf("  %-24s %10.1f ms\n", "libutf (native UTF-8)", libutf_ms);
    printf("  %-24s %10.1f ms\n", "ICU (native UTF-16)", icu_native_ms);
    if (icu_native_ms >= libutf_ms)
        printf("  %-24s %10.1fx faster\n", "libutf", icu_native_ms / libutf_ms);
    else
        printf("  %-24s %10.1fx faster\n", "ICU", libutf_ms / icu_native_ms);

    printf("\n--- Summary ---\n");
    printf("  libutf (UTF-8 native):           %8.1f ms\n", libutf_ms);
    printf("  ICU (UTF-8 convert+segment):     %8.1f ms\n", icu_utf8_ms);
    printf("  ICU (UTF-16 native):             %8.1f ms\n", icu_native_ms);

    ubrk_close(bi);
    (void)sink;
    return 0;
}
