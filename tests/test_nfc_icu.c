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
#include <stdint.h>
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

/* --- Differential fuzz: random code point sequences vs ICU --- */

static unsigned int g_rng = 0x1905;
static unsigned int xrand(void)
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng >> 8;
}

/* Alphabet biased toward composition machinery: ASCII, combining marks
 * of assorted CCC classes, Hangul jamo and precomposed syllables, Latin
 * precomposed, singletons, CJK, and SMP. */
static uint32_t fuzz_cp(void)
{
    /* Composition exclusions: starters with NFC_QC=No.  A boundary before
     * one of these is legal, and failing to take it was issue #1. */
    static const uint32_t excl[] = {
        0x0958, 0x0959, 0x095A, 0x095B, 0x095C, 0x095D, 0x095E, 0x095F,
        0x09DC, 0x09DD, 0x09DF, 0x0A33, 0x0A36, 0x0A59, 0x0A5A, 0x0A5B,
        0x0A5E, 0x0B5C, 0x0B5D, 0x0F43, 0x0F4D, 0x0F52, 0x0F57, 0x0F5C,
        0x0F69, 0x0F76, 0x0F78, 0x0F93, 0x0F9D, 0x0FA2, 0x0FA7, 0x0FAC,
        0x0FB9, 0x2ADC, 0xFB1D, 0xFB1F
    };
    /* U+0F73/75/81 are QC=No starters whose decomposition BEGINS with
     * U+0F71 (ccc=129) — the one family where a boundary before a QC=No
     * starter would be wrong.  U+0F71/0F72/0F80 are their mark partners. */
    static const uint32_t tibet[] = {
        0x0F73, 0x0F75, 0x0F81, 0x0F71, 0x0F72, 0x0F74, 0x0F80, 0x0FB5
    };

    switch (xrand() % 13) {
        case 0: return 0x20 + (xrand() % 0x5F);            /* ASCII */
        case 1: return 0x0300 + (xrand() % 0x70);          /* combining marks */
        case 2: return 0x1100 + (xrand() % 0x13);          /* jamo L */
        case 3: return 0x1161 + (xrand() % 0x15);          /* jamo V */
        case 4: return 0x11A8 + (xrand() % 0x1B);          /* jamo T */
        case 5: return 0xAC00 + (xrand() % 0x2BA4);        /* Hangul syllables */
        case 6: return 0xC0 + (xrand() % 0x140);           /* Latin-1/Ext-A */
        case 7: return (xrand() % 2) ? 0x2126 : 0x212B;    /* singletons */
        case 8: return 0x4E00 + (xrand() % 0x5000);        /* CJK */
        case 9: return excl[xrand() % (sizeof excl / sizeof *excl)];
        case 10: return tibet[xrand() % (sizeof tibet / sizeof *tibet)];
        case 11: return 0x0E30 + (xrand() % 0x60);         /* Thai/Lao */
        default: return 0x1D15E + (xrand() % 0x40);        /* SMP (musical) */
    }
}

static size_t encode_utf8(uint32_t cp, unsigned char *b)
{
    if (cp < 0x80) { b[0] = (unsigned char)cp; return 1; }
    if (cp < 0x800) { b[0] = 0xC0|(cp>>6); b[1] = 0x80|(cp&63); return 2; }
    if (cp < 0x10000) {
        b[0] = 0xE0|(cp>>12); b[1] = 0x80|((cp>>6)&63); b[2] = 0x80|(cp&63);
        return 3;
    }
    b[0] = 0xF0|(cp>>18); b[1] = 0x80|((cp>>12)&63);
    b[2] = 0x80|((cp>>6)&63); b[3] = 0x80|(cp&63);
    return 4;
}

static void fuzz_nfc(const UNormalizer2 *norm, int rounds)
{
    int mismatches = 0;
    for (int r = 0; r < rounds; r++) {
        unsigned char in[64];
        size_t inLen = 0;
        int nCps = 1 + (int)(xrand() % 12);
        for (int i = 0; i < nCps && inLen + 4 <= sizeof(in); i++)
            inLen += encode_utf8(fuzz_cp(), in + inLen);

        unsigned char dst[512];
        size_t nDst;
        utf_nfc_normalize(in, inLen, dst, sizeof(dst), &nDst);

        UErrorCode err = U_ZERO_ERROR;
        UChar usrc[256], udst[256];
        int32_t usrcLen, udstLen;
        u_strFromUTF8(usrc, 256, &usrcLen, (const char *)in,
                      (int32_t)inLen, &err);
        udstLen = unorm2_normalize(norm, usrc, usrcLen, udst, 256, &err);
        char icu_utf8[512];
        int32_t icuLen;
        u_strToUTF8(icu_utf8, 512, &icuLen, udst, udstLen, &err);
        if (U_FAILURE(err)) continue;

        if ((size_t)icuLen != nDst || memcmp(dst, icu_utf8, nDst) != 0) {
            if (mismatches < 5) {
                printf("  FAIL fuzz[%d]: in:", r);
                for (size_t i = 0; i < inLen; i++) printf(" %02x", in[i]);
                printf("\n    libutf:");
                for (size_t i = 0; i < nDst; i++) printf(" %02x", dst[i]);
                printf("\n    ICU:   ");
                for (int i = 0; i < icuLen; i++)
                    printf(" %02x", (unsigned char)icu_utf8[i]);
                printf("\n");
            }
            mismatches++;
        }
    }
    if (mismatches) {
        printf("  FAIL fuzz: %d/%d mismatched ICU\n", mismatches, rounds);
        g_fail++;
    } else {
        printf("  %d randomized inputs match ICU byte-for-byte\n", rounds);
        g_pass++;
    }
}

/* --- Long runs: the shapes from issue #1, checked against ICU at scale --- */

/* Repeated code points, tagged with whether a canonical boundary exists
 * before each one.  Where it does, every repeat is an INDEPENDENT sequence,
 * so an arbitrarily long run must normalize cleanly — reporting
 * SEGMENT_TOO_LONG there means the segmenter is again swallowing whole runs
 * (issue #1) and is a failure, not an acceptable limit.  Where it does not
 * (marks; jamo V/T and U+0F71/U+0F73, which are NFC_QC=Maybe or decompose
 * to a non-starter), the run genuinely is one sequence and the cap is a
 * legitimate answer. */
static const struct { uint32_t cp; int independent; } g_reps[] = {
    { 0x0958, 1 },  /* exclusion, decomposes to a starter */
    { 0x212B, 1 },  /* singleton ANGSTROM */
    { 0x2126, 1 },  /* singleton OHM */
    { 0x09DC, 1 },  /* Bengali exclusion */
    { 0x0F43, 1 },  /* Tibetan exclusion */
    { 0xFB1D, 1 },  /* Hebrew exclusion */
    { 0x0301, 0 },  /* combining acute */
    { 0x0323, 0 },  /* combining dot below */
    { 0x1161, 0 },  /* jamo V: NFC_QC=Maybe */
    { 0x11A8, 0 },  /* jamo T: NFC_QC=Maybe */
    { 0x0F73, 0 },  /* QC=No starter decomposing to U+0F71 (ccc=129) */
    { 0x0F71, 0 },  /* Tibetan vowel sign AA, ccc=129 */
};

static void fuzz_long_runs(const UNormalizer2 *norm, int rounds)
{
    static unsigned char in[1 << 16], dst[1 << 18];
    static char icu_utf8[1 << 18];
    static UChar usrc[1 << 16], udst[1 << 17];
    int bad = 0;

    for (int r = 0; r < rounds; r++) {
        size_t which = xrand() % (sizeof g_reps / sizeof *g_reps);
        uint32_t cp = g_reps[which].cp;
        int independent = g_reps[which].independent;
        /* Deliberately past UTF_NFC_SEG_MAX (1024) so a run that is wrongly
         * treated as one segment cannot slip under the cap. */
        int count = 1 + (int)(xrand() % 1800);
        int lead = (int)(xrand() % 3);   /* 0 none, 1 'a', 2 Tibetan KA */

        size_t inLen = 0;
        if (1 == lead) inLen += encode_utf8('a', in);
        else if (2 == lead) inLen += encode_utf8(0x0F40, in);
        for (int i = 0; i < count; i++) inLen += encode_utf8(cp, in + inLen);

        size_t nDst;
        utf_nfc_status rc = utf_nfc_normalize(in, inLen, dst, sizeof dst, &nDst);

        UErrorCode err = U_ZERO_ERROR;
        int32_t usrcLen, udstLen, icuLen;
        u_strFromUTF8(usrc, (int32_t)(sizeof usrc / sizeof *usrc), &usrcLen,
                      (const char *)in, (int32_t)inLen, &err);
        udstLen = unorm2_normalize(norm, usrc, usrcLen, udst,
                                   (int32_t)(sizeof udst / sizeof *udst), &err);
        u_strToUTF8(icu_utf8, (int32_t)sizeof icu_utf8, &icuLen, udst, udstLen, &err);
        if (U_FAILURE(err)) continue;

        if (UTF_NFC_SEGMENT_TOO_LONG == rc) {
            /* Only a genuine single sequence may exhaust the cap. */
            if (independent) {
                if (bad < 5)
                    printf("  FAIL long[%d]: U+%04X x%d — run of independent "
                           "sequences reported SEGMENT_TOO_LONG\n", r, cp, count);
                bad++;
            }
            continue;
        }

        if (UTF_NFC_OK != rc || (size_t)icuLen != nDst
            || memcmp(dst, icu_utf8, nDst) != 0) {
            if (bad < 5)
                printf("  FAIL long[%d]: U+%04X x%d lead=%d -> libutf %zu (rc=%d), ICU %d\n",
                       r, cp, count, lead, nDst, (int)rc, icuLen);
            bad++;
        }
    }
    if (bad) {
        printf("  FAIL long runs: %d/%d mismatched ICU\n", bad, rounds);
        g_fail++;
    } else {
        printf("  %d long repeated runs match ICU byte-for-byte\n", rounds);
        g_pass++;
    }
}

/* --- Status reporting (issue #2) --- */

static void check_status(const char *label, utf_nfc_status got, utf_nfc_status want)
{
    if (got != want) {
        printf("  FAIL %s: status %d, expected %d\n", label, (int)got, (int)want);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void test_status(const UNormalizer2 *norm)
{
    static unsigned char in[1 << 16], dst[1 << 18];
    unsigned char small[16];
    size_t nDst;
    (void)norm;

    /* Ample buffer: OK. */
    size_t inLen = 0;
    for (int i = 0; i < 100; i++) inLen += encode_utf8(0x0958, in + inLen);
    check_status("exclusions ample dst",
                 utf_nfc_normalize(in, inLen, dst, sizeof dst, &nDst), UTF_NFC_OK);

    /* The documented bound never truncates, even for maximum expansion. */
    check_status("at normalize_bound",
                 utf_nfc_normalize(in, inLen, dst,
                                   utf_nfc_normalize_bound(inLen), &nDst),
                 UTF_NFC_OK);

    /* Expanding input into a same-size buffer truncates — and says so.
     * This is the trap: nSrc bytes is NOT a safe destination size. */
    check_status("dst == nSrc on expanding input",
                 utf_nfc_normalize(in, inLen, dst, inLen, &nDst),
                 UTF_NFC_TRUNCATED);

    /* Short dst on a clean-run copy. */
    check_status("clean ASCII short dst",
                 utf_nfc_normalize((const unsigned char *)"hello world", 11,
                                   small, 4, &nDst),
                 UTF_NFC_TRUNCATED);

    /* Whatever is reported as written must be a valid prefix of the
     * correct answer, never a partial or half-composed sequence. */
    utf_nfc_normalize(in, inLen, dst, sizeof dst, &nDst);
    size_t full = nDst;
    static unsigned char part[1 << 18];
    size_t nPart;
    utf_nfc_normalize(in, inLen, part, 61, &nPart);
    if (nPart > full || nPart > 61 || memcmp(part, dst, nPart) != 0) {
        printf("  FAIL truncated output is not a prefix (%zu of %zu)\n", nPart, full);
        g_fail++;
    } else {
        g_pass++;
    }

    /* A combining sequence past the cap is reported, not silently cut. */
    inLen = encode_utf8('a', in);
    for (int i = 0; i < 4000; i++) inLen += encode_utf8(0x0301, in + inLen);
    check_status("oversized sequence",
                 utf_nfc_normalize(in, inLen, dst, sizeof dst, &nDst),
                 UTF_NFC_SEGMENT_TOO_LONG);

    /* A long sequence UNDER the cap still succeeds. */
    inLen = encode_utf8('a', in);
    for (int i = 0; i < 500; i++) inLen += encode_utf8(0x0301, in + inLen);
    check_status("500-mark sequence under cap",
                 utf_nfc_normalize(in, inLen, dst, sizeof dst, &nDst), UTF_NFC_OK);
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

    /* --- Blocked composition (UAX #15 D115) --- */
    /* A starter B is blocked from starter A by ANY intervening mark;
     * there is no CCC(B)=0 exemption.  An earlier bug composed Hangul
     * jamo across a mark: U+B3C4 U+032B U+11C1 became U+B3DE U+032B. */
    test_nfc("jamo_blocked_by_mark", "\xeb\x8f\x84\xcc\xab\xe1\x87\x81", norm);
    /* L jamo + mark + V jamo: LV composition must also be blocked. */
    test_nfc("lv_blocked_by_mark", "\xe1\x84\x80\xcc\xab\xe1\x85\xa1", norm);
    /* Base + grave + ring (both CCC=230): ring blocked by same-class mark,
     * so Å must not form. */
    test_nfc("mark_blocked_same_ccc", "A\xcc\x80\xcc\x8a", norm);
    /* Singleton + mark + composing mark: U+2126 U+0328 U+0301 — the acute
     * (CCC=230) is NOT blocked by ogonek (CCC=202) and composes with Ω. */
    test_nfc("mark_composes_over_lower_ccc", "\xe2\x84\xa6\xcc\xa8\xcc\x81", norm);

    /* --- Segment boundaries (issue #1) ---
     *
     * A run of QC=No starters is a run of independent sequences.  Treating
     * them as one segment let a fixed cap silently discard the overflow. */
    test_nfc("exclusion_run", "\xe0\xa5\x98\xe0\xa5\x98\xe0\xa5\x98"
                              "\xe0\xa5\x98\xe0\xa5\x98", norm);   /* 5x U+0958 */
    test_nfc("singleton_run", "\xe2\x84\xab\xe2\x84\xab\xe2\x84\xab", norm); /* 3x U+212B */
    test_nfc("exclusion_then_mark", "\xe0\xa5\x98\xcc\x81", norm); /* U+0958 U+0301 */
    test_nfc("singleton_mixed_run", "\xe2\x84\xa6\xe2\x84\xab\xe2\x84\xa6", norm);

    /* U+0F73 is a QC=No STARTER whose decomposition begins with U+0F71
     * (CCC=129), so unlike other QC=No starters there is NO boundary
     * before it: a preceding CCC=130 mark must reorder past that U+0F71.
     * These are the three code points that make "break at any non-Maybe
     * starter" wrong. */
    test_nfc("tibetan_0F73_after_mark", "\xe0\xbd\x80\xe0\xbd\xb2\xe0\xbd\xb3", norm);
    test_nfc("tibetan_0F75_after_mark", "\xe0\xbd\x80\xe0\xbd\xb2\xe0\xbd\xb5", norm);
    test_nfc("tibetan_0F81_after_mark", "\xe0\xbd\x80\xe0\xbd\xb2\xe0\xbe\x81", norm);
    test_nfc("tibetan_0F73_bare", "\xe0\xbd\xb3", norm);
    test_nfc("tibetan_stack", "\xe0\xbd\x80\xe0\xbe\x80\xe0\xbd\xb3\xe0\xbd\xb2", norm);

    printf("\n[nfc_is_nfc]\n");

    test_is_nfc("ascii", "Hello!", norm);
    test_is_nfc("precomposed", "\xc3\xa9\xc3\xa8", norm);
    test_is_nfc("combining_not_nfc", "e\xcc\x81", norm);
    test_is_nfc("hangul_precomposed", "\xed\x95\x9c", norm);
    test_is_nfc("hangul_jamo", "\xe1\x84\x80\xe1\x85\xa1", norm);
    test_is_nfc("ohm_sign", "\xe2\x84\xa6", norm);
    test_is_nfc("cjk", "\xe4\xb8\xad\xe6\x96\x87", norm);
    test_is_nfc("reordered_ccc", "a\xcc\x81\xcc\xa3", norm);

    printf("\n[nfc_status]\n");
    test_status(norm);

    printf("\n[nfc_fuzz_vs_icu]\n");
    fuzz_nfc(norm, 100000);
    fuzz_long_runs(norm, 2000);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
