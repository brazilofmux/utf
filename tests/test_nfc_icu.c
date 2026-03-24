/*
 * test_nfc_icu.c — NFC correctness tests against ICU reference.
 *
 * Build:
 *   gcc -O2 -I../include -o test_nfc_icu test_nfc_icu.c -L.. -lutf -lm \
 *       $(pkg-config --cflags --libs icu-uc)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "utf/nfc.h"
#include <unicode/unorm2.h>
#include <unicode/ustring.h>
#include <unicode/utypes.h>

static int g_pass = 0, g_fail = 0;

static void test_nfc(const char *label, const char *input,
                     const UNormalizer2 *norm)
{
    size_t inLen = strlen(input);

    /* libutf */
    unsigned char dst[8192];
    size_t nDst;
    utf_nfc_normalize((const unsigned char *)input, inLen,
                      dst, sizeof(dst), &nDst);

    /* ICU reference */
    UErrorCode err = U_ZERO_ERROR;
    UChar usrc[8192], udst[8192];
    int32_t usrcLen, udstLen;
    u_strFromUTF8(usrc, 8192, &usrcLen, input, (int32_t)inLen, &err);
    udstLen = unorm2_normalize(norm, usrc, usrcLen, udst, 8192, &err);
    char icu_utf8[8192];
    int32_t icu_utf8_len;
    u_strToUTF8(icu_utf8, 8192, &icu_utf8_len, udst, udstLen, &err);

    if ((size_t)icu_utf8_len != nDst || memcmp(dst, icu_utf8, nDst) != 0) {
        printf("  FAIL %s: libutf=%zu bytes, ICU=%d bytes\n",
               label, nDst, icu_utf8_len);
        /* Hex dump first 20 bytes of each */
        printf("    libutf: ");
        for (size_t i = 0; i < nDst && i < 20; i++) printf("%02x ", dst[i]);
        printf("\n    ICU:    ");
        for (int i = 0; i < icu_utf8_len && i < 20; i++)
            printf("%02x ", (unsigned char)icu_utf8[i]);
        printf("\n");
        g_fail++;
    } else {
        g_pass++;
    }
}

static void test_is_nfc(const char *label, const char *input,
                        const UNormalizer2 *norm)
{
    size_t inLen = strlen(input);
    int libutf_r = utf_nfc_is_nfc((const unsigned char *)input, inLen);

    UErrorCode err = U_ZERO_ERROR;
    UChar usrc[8192];
    int32_t usrcLen;
    u_strFromUTF8(usrc, 8192, &usrcLen, input, (int32_t)inLen, &err);
    UBool icu_r = unorm2_isNormalized(norm, usrc, usrcLen, &err);

    if (libutf_r != (int)icu_r) {
        printf("  FAIL is_nfc %s: libutf=%d ICU=%d\n",
               label, libutf_r, (int)icu_r);
        g_fail++;
    } else {
        g_pass++;
    }
}

int main(void)
{
    UErrorCode err = U_ZERO_ERROR;
    const UNormalizer2 *norm = unorm2_getNFCInstance(&err);
    if (U_FAILURE(err)) {
        fprintf(stderr, "ICU error: %s\n", u_errorName(err));
        return 1;
    }

    printf("[nfc_normalize]\n");

    /* --- Already NFC --- */
    test_nfc("empty", "", norm);
    test_nfc("ascii", "Hello, World!", norm);
    test_nfc("long_ascii", "The quick brown fox jumps over the lazy dog.", norm);
    test_nfc("precomposed_latin", "\xc3\xa9\xc3\xa8\xc3\xaa\xc3\xab", norm);
    test_nfc("cjk", "\xe4\xb8\xad\xe6\x96\x87\xe6\xb5\x8b\xe8\xaf\x95", norm);
    test_nfc("hangul_precomposed", "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4", norm);
    test_nfc("greek", "\xce\xba\xce\xb1\xce\xbb\xce\xb7\xce\xbc\xce\xad\xcf\x81\xce\xb1", norm);
    test_nfc("german", "\xc3\xa4pfel \xc3\xbc""ber stra\xc3\x9f""e", norm);

    /* --- Needs normalization (combining marks) --- */
    test_nfc("combining_acute", "caf\x65\xcc\x81", norm);
    test_nfc("combining_resume", "r\x65\xcc\x81sum\x65\xcc\x81", norm);
    test_nfc("angstrom_mixed", "\x41\xcc\x8a\x6e\x67\x73\x74\x72\xc3\xb6\x6d", norm);
    test_nfc("a_cedilla_acute", "a\xcc\xa7\xcc\x81", norm);
    test_nfc("a_acute_cedilla", "a\xcc\x81\xcc\xa7", norm);

    /* --- Multiple combining marks (CCC reordering) --- */
    /* U+0061 + U+0323 (below, ccc=220) + U+0301 (above, ccc=230) */
    test_nfc("below_above", "a\xcc\xa3\xcc\x81", norm);
    /* U+0061 + U+0301 (above, ccc=230) + U+0323 (below, ccc=220) — needs reorder */
    test_nfc("above_below_reorder", "a\xcc\x81\xcc\xa3", norm);
    /* U+0061 + U+0308 (diaeresis, ccc=230) + U+0301 (acute, ccc=230) — stable, no reorder */
    test_nfc("same_ccc", "a\xcc\x88\xcc\x81", norm);

    /* --- Hangul algorithmic --- */
    /* Jamo L + V (should compose to LV syllable) */
    test_nfc("hangul_lv", "\xe1\x84\x80\xe1\x85\xa1", norm);  /* ᄀ + ᅡ → 가 */
    /* Jamo L + V + T */
    test_nfc("hangul_lvt", "\xe1\x84\x80\xe1\x85\xa1\xe1\x86\xa8", norm); /* ᄀ + ᅡ + ᆨ → 각 */
    /* Precomposed LV + T */
    test_nfc("hangul_lv_plus_t", "\xea\xb0\x80\xe1\x86\xa8", norm); /* 가 + ᆨ → 각 */

    /* --- Longer mixed strings --- */
    test_nfc("mixed_long",
             "The caf\x65\xcc\x81 serves cr\xc3\xa8me br\xc3\xbb"
             "l\x65\xcc\x81\x65 and pi\xc3\xb1\x61 colada.", norm);

    /* --- Edge: singleton decompositions --- */
    /* U+2126 OHM SIGN → U+03A9 GREEK CAPITAL LETTER OMEGA */
    test_nfc("ohm_sign", "\xe2\x84\xa6", norm);
    /* U+212B ANGSTROM SIGN → U+00C5 LATIN CAPITAL LETTER A WITH RING ABOVE */
    test_nfc("angstrom_sign", "\xe2\x84\xab", norm);
    /* U+00C5 is already NFC */
    test_nfc("a_ring_precomposed", "\xc3\x85", norm);

    printf("\n[nfc_is_nfc]\n");

    test_is_nfc("ascii", "Hello!", norm);
    test_is_nfc("precomposed", "\xc3\xa9\xc3\xa8", norm);
    test_is_nfc("combining_not_nfc", "e\xcc\x81", norm);
    test_is_nfc("hangul_precomposed", "\xed\x95\x9c", norm);
    test_is_nfc("hangul_jamo", "\xe1\x84\x80\xe1\x85\xa1", norm);
    test_is_nfc("ohm_sign", "\xe2\x84\xa6", norm);
    test_is_nfc("cjk", "\xe4\xb8\xad\xe6\x96\x87", norm);
    test_is_nfc("reordered_ccc", "a\xcc\x81\xcc\xa3", norm);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
