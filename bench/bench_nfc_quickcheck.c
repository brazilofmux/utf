/*
 * bench_nfc_quickcheck.c — NFC quick-check (already-NFC) benchmark.
 *
 * Most real-world text is already NFC. This benchmarks the fast path.
 *
 * Build:
 *   gcc -O2 -I../include -o bench_nfc_quickcheck bench_nfc_quickcheck.c \
 *       -L.. -lutf -lm $(pkg-config --cflags --libs icu-uc)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "utf/nfc.h"

#include <unicode/unorm2.h>
#include <unicode/ustring.h>
#include <unicode/utypes.h>

/* Already-NFC strings (the common case). */
static const char *test_strings[] = {
    "Hello, World!",
    "The quick brown fox jumps over the lazy dog.",
    "\xc3\xa9\xc3\xa8\xc3\xaa\xc3\xab",          /* éèêë precomposed */
    "\xe4\xb8\xad\xe6\x96\x87\xe6\xb5\x8b\xe8\xaf\x95",  /* 中文测试 */
    "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4",       /* 한국어 */
    "\xc3\xa4pfel \xc3\xbc""ber stra\xc3\x9f""e",  /* äpfel über straße */
    "\xce\xba\xce\xb1\xce\xbb\xce\xb7\xce\xbc\xce\xad\xce\xb1",  /* καλημέρα */
    "ASCII only string no special chars at all just plain text here",
    "Longer string with \xc3\xa9 and \xc3\xa0 and \xc3\xbc mixed in.",
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
    int iterations = (argc > 1) ? atoi(argv[1]) : 500000;
    volatile int sink = 0;

    printf("NFC Quick-Check (already NFC) Benchmark\n");
    printf("Iterations: %d per string, %d strings\n\n", iterations, nStrings);

    /* --- libutf: utf_nfc_is_nfc --- */
    double t0 = now_sec();
    for (int iter = 0; iter < iterations; iter++) {
        for (int s = 0; s < nStrings; s++) {
            sink += utf_nfc_is_nfc((const unsigned char *)test_strings[s],
                                   strlen(test_strings[s]));
        }
    }
    double t1 = now_sec();
    double libutf_ms = (t1 - t0) * 1000.0;

    /* --- ICU: unorm2_isNormalized --- */
    UErrorCode err = U_ZERO_ERROR;
    const UNormalizer2 *norm = unorm2_getNFCInstance(&err);
    UChar usrc[8192];
    int32_t usrcLen;

    t0 = now_sec();
    for (int iter = 0; iter < iterations; iter++) {
        for (int s = 0; s < nStrings; s++) {
            u_strFromUTF8(usrc, 8192, &usrcLen,
                          test_strings[s], (int32_t)strlen(test_strings[s]), &err);
            sink += unorm2_isNormalized(norm, usrc, usrcLen, &err);
        }
    }
    t1 = now_sec();
    double icu_ms = (t1 - t0) * 1000.0;

    printf("%-20s %10.1f ms\n", "libutf", libutf_ms);
    printf("%-20s %10.1f ms\n", "ICU 74.2", icu_ms);
    printf("%-20s %10.1fx\n", "Speedup", icu_ms / libutf_ms);

    (void)sink;
    return 0;
}
