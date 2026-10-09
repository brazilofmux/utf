/* icukey LOCALE: one UTF-8 string per stdin line -> ICU tertiary sort key (hex). */
#include <unicode/ucol.h>
#include <unicode/ustring.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
    UErrorCode e = U_ZERO_ERROR;
    UCollator *c = ucol_open(argv[1], &e);
    if (U_FAILURE(e)) { fprintf(stderr, "open %s: %s\n", argv[1], u_errorName(e)); return 1; }
    { UErrorCode e2 = U_ZERO_ERROR; fprintf(stderr, "%s\n", ucol_getLocaleByType(c, ULOC_ACTUAL_LOCALE, &e2)); }
    ucol_setStrength(c, argc > 2 ? UCOL_SECONDARY : UCOL_TERTIARY);
    char line[4096]; UChar u[2048]; uint8_t key[8192];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\n")] = 0;
        int32_t n; e = U_ZERO_ERROR;
        u_strFromUTF8(u, 2048, &n, line, -1, &e);
        int32_t k = ucol_getSortKey(c, u, n, key, sizeof key);
        for (int i = 0; i < k - 1; i++) printf("%02x", key[i]);
        printf("\n");
    }
    return 0;
}
