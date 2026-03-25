/*
 * test_collate_icu.c — DUCET collation correctness tests against ICU.
 *
 * Build:
 *   gcc -O2 -I../include -o test_collate_icu test_collate_icu.c \
 *       -L.. -lutf -lm $(pkg-config --cflags --libs icu-uc icu-i18n)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "utf/collate.h"
#include <unicode/ucol.h>
#include <unicode/ustring.h>
#include <unicode/utypes.h>

static int g_pass = 0, g_fail = 0;

static int sign(int x) { return (x > 0) - (x < 0); }

static void test_cmp(const char *label,
                     const char *a, const char *b,
                     UCollator *coll)
{
    int lr = utf_collate_cmp((const unsigned char *)a, strlen(a),
                              (const unsigned char *)b, strlen(b));
    UErrorCode err = U_ZERO_ERROR;
    UChar ua[512], ub[512];
    int32_t ual, ubl;
    u_strFromUTF8(ua, 512, &ual, a, (int32_t)strlen(a), &err);
    u_strFromUTF8(ub, 512, &ubl, b, (int32_t)strlen(b), &err);
    UCollationResult ir = ucol_strcoll(coll, ua, ual, ub, ubl);

    int ls = sign(lr);
    int is = (ir == UCOL_LESS) ? -1 : (ir == UCOL_GREATER) ? 1 : 0;

    if (ls != is) {
        printf("  FAIL %s: \"%s\" vs \"%s\": libutf=%d ICU=%d\n",
               label, a, b, ls, is);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void test_cmp_bytes(const char *label,
                           const unsigned char *a, size_t nA,
                           const unsigned char *b, size_t nB,
                           UCollator *coll)
{
    int lr = utf_collate_cmp(a, nA, b, nB);
    UErrorCode err = U_ZERO_ERROR;
    UChar ua[8192], ub[8192];
    int32_t ual, ubl;
    u_strFromUTF8(ua, 8192, &ual, (const char *)a, (int32_t)nA, &err);
    u_strFromUTF8(ub, 8192, &ubl, (const char *)b, (int32_t)nB, &err);
    UCollationResult ir = ucol_strcoll(coll, ua, ual, ub, ubl);

    int ls = sign(lr);
    int is = (ir == UCOL_LESS) ? -1 : (ir == UCOL_GREATER) ? 1 : 0;

    if (ls != is) {
        printf("  FAIL %s: libutf=%d ICU=%d\n",
               label, ls, is);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void test_ci(const char *label,
                    const char *a, const char *b,
                    int expected_sign)
{
    int lr = utf_collate_cmp_ci((const unsigned char *)a, strlen(a),
                                 (const unsigned char *)b, strlen(b));
    int ls = sign(lr);
    if (ls != expected_sign) {
        printf("  FAIL ci %s: \"%s\" vs \"%s\": got=%d expected=%d\n",
               label, a, b, ls, expected_sign);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void test_sortkey_order(const char *label,
                               const char *a, const char *b,
                               UCollator *coll)
{
    (void)coll;
    unsigned char ka[1024], kb[1024];
    size_t kla = utf_collate_sortkey((const unsigned char *)a, strlen(a),
                                      ka, sizeof(ka));
    size_t klb = utf_collate_sortkey((const unsigned char *)b, strlen(b),
                                      kb, sizeof(kb));
    int key_cmp = memcmp(ka, kb, (kla < klb) ? kla : klb);
    if (0 == key_cmp) key_cmp = (kla > klb) - (kla < klb);
    int key_sign = sign(key_cmp);

    int cmp_sign = sign(utf_collate_cmp(
        (const unsigned char *)a, strlen(a),
        (const unsigned char *)b, strlen(b)));

    if (key_sign != cmp_sign) {
        printf("  FAIL sortkey %s: cmp=%d sortkey=%d\n",
               label, cmp_sign, key_sign);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void test_sortkey_matches_cmp(const char *label,
                                     const unsigned char *a, size_t nA,
                                     const unsigned char *b, size_t nB)
{
    unsigned char ka[16384], kb[16384];
    size_t kla = utf_collate_sortkey(a, nA, ka, sizeof(ka));
    size_t klb = utf_collate_sortkey(b, nB, kb, sizeof(kb));
    int key_cmp = memcmp(ka, kb, (kla < klb) ? kla : klb);
    if (0 == key_cmp) key_cmp = (kla > klb) - (kla < klb);
    int key_sign = sign(key_cmp);

    int cmp_sign = sign(utf_collate_cmp(a, nA, b, nB));

    if (key_sign != cmp_sign) {
        printf("  FAIL sortkey_match %s: cmp=%d sortkey=%d\n",
               label, cmp_sign, key_sign);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void test_sortkey_ci_matches_cmp_ci(const char *label,
                                           const unsigned char *a, size_t nA,
                                           const unsigned char *b, size_t nB)
{
    unsigned char ka[16384], kb[16384];
    size_t kla = utf_collate_sortkey_ci(a, nA, ka, sizeof(ka));
    size_t klb = utf_collate_sortkey_ci(b, nB, kb, sizeof(kb));
    int key_cmp = memcmp(ka, kb, (kla < klb) ? kla : klb);
    if (0 == key_cmp) key_cmp = (kla > klb) - (kla < klb);
    int key_sign = sign(key_cmp);

    int cmp_sign = sign(utf_collate_cmp_ci(a, nA, b, nB));

    if (key_sign != cmp_sign) {
        printf("  FAIL sortkey_ci %s: cmp=%d sortkey=%d\n",
               label, cmp_sign, key_sign);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void test_ci_bytes(const char *label,
                          const unsigned char *a, size_t nA,
                          const unsigned char *b, size_t nB,
                          int expected_sign)
{
    int lr = utf_collate_cmp_ci(a, nA, b, nB);
    int ls = sign(lr);
    if (ls != expected_sign) {
        printf("  FAIL ci %s: got=%d expected=%d\n",
               label, ls, expected_sign);
        g_fail++;
    } else {
        g_pass++;
    }
}

int main(void)
{
    UErrorCode err = U_ZERO_ERROR;
    UCollator *coll = ucol_open("", &err);
    if (U_FAILURE(err)) {
        fprintf(stderr, "ICU error: %s\n", u_errorName(err));
        return 1;
    }

    printf("[collate_cmp vs ICU]\n");

    /* --- Basic ASCII ordering --- */
    test_cmp("a_vs_b", "a", "b", coll);
    test_cmp("z_vs_a", "z", "a", coll);
    test_cmp("same", "abc", "abc", coll);
    test_cmp("prefix", "abc", "abcd", coll);
    test_cmp("prefix_rev", "abcd", "abc", coll);
    test_cmp("empty_vs_a", "", "a", coll);
    test_cmp("empty_vs_empty", "", "", coll);
    test_cmp("apple_banana", "apple", "banana", coll);

    /* --- Case differences (level 3) --- */
    test_cmp("case_Aa", "A", "a", coll);
    test_cmp("case_aA", "a", "A", coll);
    test_cmp("case_Hello", "Hello", "hello", coll);
    test_cmp("case_ABC", "ABC", "abc", coll);

    /* --- Accent differences (level 2) --- */
    test_cmp("accent_e_eacute", "e", "\xc3\xa9", coll);  /* e vs é */
    test_cmp("accent_cafe", "cafe", "caf\xc3\xa9", coll); /* cafe vs café */
    test_cmp("accent_naive", "naive", "na\xc3\xafve", coll); /* naive vs naïve */
    test_cmp("accent_resume", "resume", "r\xc3\xa9sum\xc3\xa9", coll);

    /* Decomposed vs precomposed (should be equal at all levels) */
    test_cmp("decomp_vs_precomp",
             "caf\x65\xcc\x81",    /* cafe + combining acute */
             "caf\xc3\xa9", coll);  /* café precomposed */

    /* --- Accented case combinations --- */
    test_cmp("Cafe_cafe", "Caf\xc3\xa9", "caf\xc3\xa9", coll);  /* Café vs café */
    test_cmp("CAFE_cafe", "CAF\xc3\x89", "caf\xc3\xa9", coll);  /* CAFÉ vs café */

    /* --- German --- */
    test_cmp("strasse", "stra\xc3\x9f""e", "strasse", coll);  /* straße vs strasse */
    test_cmp("uber", "\xc3\xbc""ber", "uber", coll);  /* über vs uber */

    /* --- Scandinavian --- */
    test_cmp("angstrom", "\xc3\x85ngstr\xc3\xb6m", "angstrom", coll);

    /* --- CJK --- */
    test_cmp("cjk",
             "\xe4\xb8\xad\xe6\x96\x87",      /* 中文 */
             "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", coll);  /* 日本語 */

    /* --- Hangul --- */
    test_cmp("hangul",
             "\xed\x95\x9c\xea\xb5\xad",  /* 한국 */
             "\xec\xa4\x91\xea\xb5\xad", coll);  /* 중국 */

    /* --- Mixed scripts --- */
    test_cmp("latin_vs_cyrillic", "a", "\xd0\xb0", coll);  /* a vs а */
    test_cmp("latin_vs_greek", "a", "\xce\xb1", coll);  /* a vs α */

    /* --- Combining mark sequences --- */
    test_cmp("combining_order",
             "a\xcc\xa3\xcc\x81",   /* a + dot below + acute */
             "a\xcc\x81\xcc\xa3", coll);  /* a + acute + dot below (should be equal after reorder) */

    /* --- Numeric strings --- */
    test_cmp("num_1_2", "1", "2", coll);
    test_cmp("num_9_10", "9", "10", coll);

    printf("\n[collate_cmp_ci]\n");

    test_ci("ci_same_case", "hello", "hello", 0);
    test_ci("ci_diff_case", "Hello", "hello", 0);
    test_ci("ci_diff_char", "a", "b", -1);
    test_ci("ci_accent", "cafe", "caf\xc3\xa9", -1);
    test_sortkey_ci_matches_cmp_ci("skci_case_fold",
                                   (const unsigned char *)"Hello", 5,
                                   (const unsigned char *)"hello", 5);
    test_sortkey_ci_matches_cmp_ci("skci_accent",
                                   (const unsigned char *)"cafe", 4,
                                   (const unsigned char *)"caf\xc3\xa9", 5);

    printf("\n[sortkey consistency]\n");

    test_sortkey_order("sk_a_b", "a", "b", coll);
    test_sortkey_order("sk_case", "A", "a", coll);
    test_sortkey_order("sk_accent", "e", "\xc3\xa9", coll);
    test_sortkey_order("sk_prefix", "abc", "abcd", coll);
    test_sortkey_order("sk_same", "abc", "abc", coll);
    test_sortkey_order("sk_hangul",
                       "\xed\x95\x9c\xea\xb5\xad",
                       "\xec\xa4\x91\xea\xb5\xad", coll);

    /* --- Regression: tiebreaker and long-input handling --- */
    {
        const unsigned char plain_a[] = "a";
        const unsigned char cgj_a[] = "a\xCD\x8F";
        test_sortkey_matches_cmp("sk_tiebreaker_cgj",
                                 plain_a, strlen((const char *)plain_a),
                                 cgj_a, strlen((const char *)cgj_a));
    }

    {
        unsigned char longa[5002], longb[5002];
        memset(longa, 'a', 5000);
        memset(longb, 'a', 5000);
        longa[5000] = 'e';
        longa[5001] = '\0';
        longb[5000] = 'f';
        longb[5001] = '\0';
        test_cmp("long_tail_primary",
                 (const char *)longa, (const char *)longb, coll);
        test_ci_bytes("long_tail_primary_ci",
                      longa, 5001, longb, 5001, -1);
        test_sortkey_matches_cmp("sk_long_tail_primary",
                                 longa, 5001, longb, 5001);
        test_sortkey_ci_matches_cmp_ci("skci_long_tail_primary",
                                       longa, 5001, longb, 5001);
    }

    {
        unsigned char longa[259], longb[258];
        memset(longa, 'a', 256);
        memset(longb, 'a', 256);
        longa[256] = 0xC3;
        longa[257] = 0xA9;
        longa[258] = '\0';
        longb[256] = 'f';
        longb[257] = '\0';
        test_cmp_bytes("long_tail_nonlatin_primary",
                       longa, 258, longb, 257, coll);
    }

    ucol_close(coll);
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
