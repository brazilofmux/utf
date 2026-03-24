/*
 * bench_toupper.c — Case mapping benchmark: libutf vs ICU.
 *
 * Build:
 *   gcc -O2 -I../include -o bench_toupper bench_toupper.c -L.. -lutf -lm \
 *       $(pkg-config --cflags --libs icu-uc)
 *
 * Usage: ./bench_toupper [iterations]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "utf/color_ops.h"

/* ICU headers */
#include <unicode/ustring.h>
#include <unicode/ucasemap.h>
#include <unicode/utypes.h>

static const char *test_strings[] = {
    "hello world",
    "the quick brown fox jumps over the lazy dog",
    "caf\xc3\xa9 cr\xc3\xa8me br\xc3\xbbl\xc3\xa9\x65",
    "\xc3\xa4pfel \xc3\xbc\x62\x65r stra\xc3\x9f\x65",  /* äpfel über straße */
    "\xce\xba\xce\xb1\xce\xbb\xce\xb7\xce\xbc\xce\xad\xce\xb1",  /* καλημέρα */
    "\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82",  /* привет */
    "abcdefghijklmnopqrstuvwxyz 0123456789",
    "ALREADY UPPERCASE STAYS THE SAME",
    "MiXeD cAsE iNpUt TeStInG hErE",
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

    printf("Case Mapping (toupper) Benchmark\n");
    printf("Iterations: %d per string, %d strings\n\n", iterations, nStrings);

    /* --- libutf (co_toupper via DFA) --- */
    unsigned char dst[8192];
    double t0 = now_sec();
    for (int iter = 0; iter < iterations; iter++) {
        for (int s = 0; s < nStrings; s++) {
            const unsigned char *src = (const unsigned char *)test_strings[s];
            co_toupper(dst, src, strlen(test_strings[s]));
        }
    }
    double t1 = now_sec();
    double libutf_ms = (t1 - t0) * 1000.0;

    /* --- ICU (ucasemap) --- */
    UErrorCode err = U_ZERO_ERROR;
    UCaseMap *csm = ucasemap_open("", 0, &err);
    if (U_FAILURE(err)) {
        fprintf(stderr, "ICU error: %s\n", u_errorName(err));
        return 1;
    }

    char icu_dst[8192];
    t0 = now_sec();
    for (int iter = 0; iter < iterations; iter++) {
        for (int s = 0; s < nStrings; s++) {
            ucasemap_utf8ToUpper(csm, icu_dst, sizeof(icu_dst),
                                 test_strings[s], (int32_t)strlen(test_strings[s]),
                                 &err);
        }
    }
    t1 = now_sec();
    double icu_ms = (t1 - t0) * 1000.0;

    ucasemap_close(csm);

    printf("%-20s %10.1f ms\n", "libutf (DFA)", libutf_ms);
    printf("%-20s %10.1f ms\n", "ICU 74.2", icu_ms);
    printf("%-20s %10.1fx\n", "Speedup", icu_ms / libutf_ms);

    return 0;
}
