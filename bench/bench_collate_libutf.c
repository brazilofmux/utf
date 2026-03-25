/*
 * bench_collate_libutf.c — Focused libutf collation microbenchmarks.
 *
 * Measures utf_collate_cmp and utf_collate_sortkey on representative cases:
 *   - Latin fast path
 *   - Equal Latin strings
 *   - Non-Latin bounded path
 *   - Long-string overflow fallback
 *
 * Build:
 *   gcc -O2 -I../include -o bench_collate_libutf bench_collate_libutf.c \
 *       -L.. -lutf -lm
 *
 * Usage: ./bench_collate_libutf [iterations]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "utf/collate.h"

typedef struct {
    const char *name;
    const unsigned char *a;
    size_t nA;
    const unsigned char *b;
    size_t nB;
} cmp_case_t;

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void run_cmp_case(const cmp_case_t *tc, int iterations)
{
    volatile int sink = 0;
    double t0 = now_sec();
    for (int i = 0; i < iterations; i++) {
        sink += utf_collate_cmp(tc->a, tc->nA, tc->b, tc->nB);
    }
    double t1 = now_sec();
    printf("  %-24s %10.1f ns/call\n",
           tc->name, (t1 - t0) * 1e9 / iterations);
    (void)sink;
}

static void run_sortkey_case(const char *name,
                             const unsigned char *s, size_t nS,
                             int iterations)
{
    volatile size_t sink = 0;
    unsigned char key[16384];
    double t0 = now_sec();
    for (int i = 0; i < iterations; i++) {
        sink += utf_collate_sortkey(s, nS, key, sizeof(key));
    }
    double t1 = now_sec();
    printf("  %-24s %10.1f ns/call\n",
           name, (t1 - t0) * 1e9 / iterations);
    (void)sink;
}

int main(int argc, char **argv)
{
    int iterations = (argc > 1) ? atoi(argv[1]) : 1000000;

    static const unsigned char latin_a[] = "Caf\xC3\xA9";
    static const unsigned char latin_b[] = "caf\xC3\xA9";
    static const unsigned char equal_a[] = "The quick brown fox";
    static const unsigned char equal_b[] = "The quick brown fox";
    static const unsigned char cjk_a[] = "\xE4\xB8\xAD\xE6\x96\x87";
    static const unsigned char cjk_b[] = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
    static unsigned char long_a[5002];
    static unsigned char long_b[5002];

    memset(long_a, 'a', 5000);
    memset(long_b, 'a', 5000);
    long_a[5000] = 'e';
    long_a[5001] = '\0';
    long_b[5000] = 'f';
    long_b[5001] = '\0';

    const cmp_case_t cmp_cases[] = {
        { "latin_fast", latin_a, sizeof(latin_a) - 1, latin_b, sizeof(latin_b) - 1 },
        { "latin_equal", equal_a, sizeof(equal_a) - 1, equal_b, sizeof(equal_b) - 1 },
        { "cjk_bounded", cjk_a, sizeof(cjk_a) - 1, cjk_b, sizeof(cjk_b) - 1 },
        { "overflow_long", long_a, 5001, long_b, 5001 },
    };

    printf("libutf Collation Microbench\n");
    printf("Iterations: %d\n", iterations);

    printf("\n[utf_collate_cmp]\n");
    for (size_t i = 0; i < sizeof(cmp_cases) / sizeof(cmp_cases[0]); i++)
        run_cmp_case(&cmp_cases[i], iterations);

    printf("\n[utf_collate_sortkey]\n");
    run_sortkey_case("sortkey_latin", latin_a, sizeof(latin_a) - 1, iterations);
    run_sortkey_case("sortkey_equal", equal_a, sizeof(equal_a) - 1, iterations);
    run_sortkey_case("sortkey_cjk", cjk_a, sizeof(cjk_a) - 1, iterations);
    run_sortkey_case("sortkey_long", long_a, 5001, iterations / 10 > 0 ? iterations / 10 : 1);

    return 0;
}
