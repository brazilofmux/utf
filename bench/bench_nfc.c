/*
 * bench_nfc.c — NFC normalization benchmark: libutf vs ICU.
 *
 * Two scenarios:
 *   1. UTF-8 input (libutf's native format)
 *   2. UTF-16 input (ICU's native format)
 *
 * In each scenario, the non-native library pays for conversion.
 * This gives a fair picture of where each library wins.
 *
 * Build:
 *   gcc -O2 -I../include -o bench_nfc bench_nfc.c -L.. -lutf -lm \
 *       $(pkg-config --cflags --libs icu-uc)
 *
 * Usage: ./bench_nfc [iterations]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "utf/nfc.h"

/* ICU headers */
#include <unicode/unorm2.h>
#include <unicode/ustring.h>
#include <unicode/utypes.h>

/* Test strings: a mix of already-NFC and needs-normalization. */
static const char *test_strings[] = {
    /* Already NFC (fast path) */
    "Hello, World!",
    "The quick brown fox jumps over the lazy dog.",
    "\xc3\xa9\xc3\xa8\xc3\xaa\xc3\xab",  /* éèêë (precomposed) */
    "\xe4\xb8\xad\xe6\x96\x87\xe6\xb5\x8b\xe8\xaf\x95",  /* 中文测试 */

    /* Needs normalization (decomposed forms) */
    "caf\x65\xcc\x81",                      /* cafe + combining acute */
    "r\x65\xcc\x81sum\x65\xcc\x81",         /* resume with combining */
    "na\xc3\xaf\x76\x65",                   /* naïve (precomposed i-diaeresis) */
    "\x41\xcc\x8a\x6e\x67\x73\x74\x72\xc3\xb6\x6d", /* Ångström mixed */

    /* Hangul (algorithmic path) */
    "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4",  /* 한국어 */

    /* Longer string with mixed content */
    "The caf\x65\xcc\x81 serves cr\xc3\xa8me br\xc3\xbb"
    "l\x65\xcc\x81\x65 and pi\xc3\xb1\x61 colada.",
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
    int iterations = (argc > 1) ? atoi(argv[1]) : 100000;
    UErrorCode err = U_ZERO_ERROR;
    const UNormalizer2 *norm = unorm2_getNFCInstance(&err);
    if (U_FAILURE(err)) {
        fprintf(stderr, "ICU error: %s\n", u_errorName(err));
        return 1;
    }

    unsigned char dst8[8192];
    UChar usrc[8192], udst[8192];
    int32_t usrcLen, udstLen;
    size_t nDst;

    /* Pre-convert test strings to UTF-16 for Scenario 2. */
    UChar  u16_strings[10][512];
    int32_t u16_lens[10];
    size_t utf8_lens[10];
    for (int s = 0; s < nStrings; s++) {
        utf8_lens[s] = strlen(test_strings[s]);
        u_strFromUTF8(u16_strings[s], 512, &u16_lens[s],
                      test_strings[s], (int32_t)utf8_lens[s], &err);
    }

    printf("NFC Normalization Benchmark\n");
    printf("Iterations: %d per string, %d strings\n", iterations, nStrings);

    /* ================================================================
     * Scenario 1: UTF-8 input (libutf native, ICU pays for conversion)
     * ================================================================ */
    printf("\n--- Scenario 1: UTF-8 input ---\n");

    /* libutf: UTF-8 in, UTF-8 out (native). */
    double t0 = now_sec();
    for (int iter = 0; iter < iterations; iter++) {
        for (int s = 0; s < nStrings; s++) {
            utf_nfc_normalize((const unsigned char *)test_strings[s],
                              utf8_lens[s], dst8, sizeof(dst8), &nDst);
        }
    }
    double t1 = now_sec();
    double libutf_utf8 = (t1 - t0) * 1000.0;

    /* ICU: UTF-8 -> UTF-16 -> normalize -> UTF-16 result. */
    t0 = now_sec();
    for (int iter = 0; iter < iterations; iter++) {
        for (int s = 0; s < nStrings; s++) {
            u_strFromUTF8(usrc, 8192, &usrcLen,
                          test_strings[s], (int32_t)utf8_lens[s], &err);
            udstLen = unorm2_normalize(norm, usrc, usrcLen, udst, 8192, &err);
        }
    }
    t1 = now_sec();
    double icu_utf8 = (t1 - t0) * 1000.0;

    printf("  %-20s %10.1f ms\n", "libutf (native)", libutf_utf8);
    printf("  %-20s %10.1f ms\n", "ICU (convert+norm)", icu_utf8);
    if (icu_utf8 >= libutf_utf8)
        printf("  %-20s %10.1fx faster\n", "libutf", icu_utf8 / libutf_utf8);
    else
        printf("  %-20s %10.1fx faster\n", "ICU", libutf_utf8 / icu_utf8);

    /* ================================================================
     * Scenario 2: UTF-16 input (ICU native, libutf pays for conversion)
     * ================================================================ */
    printf("\n--- Scenario 2: UTF-16 input ---\n");

    /* ICU: UTF-16 in, UTF-16 out (native). */
    t0 = now_sec();
    for (int iter = 0; iter < iterations; iter++) {
        for (int s = 0; s < nStrings; s++) {
            udstLen = unorm2_normalize(norm, u16_strings[s], u16_lens[s],
                                       udst, 8192, &err);
        }
    }
    t1 = now_sec();
    double icu_utf16 = (t1 - t0) * 1000.0;

    /* libutf: UTF-16 -> UTF-8 -> normalize -> UTF-8 result. */
    t0 = now_sec();
    for (int iter = 0; iter < iterations; iter++) {
        for (int s = 0; s < nStrings; s++) {
            int32_t u8len;
            u_strToUTF8((char *)dst8, 8192, &u8len,
                        u16_strings[s], u16_lens[s], &err);
            utf_nfc_normalize(dst8, (size_t)u8len,
                              dst8 + 4096, 4096, &nDst);
        }
    }
    t1 = now_sec();
    double libutf_utf16 = (t1 - t0) * 1000.0;

    printf("  %-20s %10.1f ms\n", "ICU (native)", icu_utf16);
    printf("  %-20s %10.1f ms\n", "libutf (convert+norm)", libutf_utf16);
    if (icu_utf16 >= libutf_utf16)
        printf("  %-20s %10.1fx faster\n", "libutf", icu_utf16 / libutf_utf16);
    else
        printf("  %-20s %10.1fx faster\n", "ICU", libutf_utf16 / icu_utf16);

    printf("\n--- Summary ---\n");
    printf("  libutf NFC (UTF-8 native):    %8.1f ms\n", libutf_utf8);
    printf("  ICU NFC (UTF-16 native):      %8.1f ms\n", icu_utf16);
    if (icu_utf16 >= libutf_utf8)
        printf("  Core-to-core:                  libutf %.1fx faster\n",
               icu_utf16 / libutf_utf8);
    else
        printf("  Core-to-core:                  ICU %.1fx faster\n",
               libutf_utf8 / icu_utf16);

    return 0;
}
