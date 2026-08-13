/*
 * nfc_check.c — Check and normalize UTF-8 strings to NFC.
 *
 * Demonstrates utf_nfc_is_nfc() and utf_nfc_normalize().
 *
 * Build: gcc -O2 -I../include -o nfc_check nfc_check.c -L.. -lutf -lm
 * Usage: echo "café" | ./nfc_check
 *        printf '\x65\xcc\x81' | ./nfc_check   # e + combining acute
 */

#include <stdio.h>
#include <string.h>
#include "utf/nfc.h"

int main(void)
{
    unsigned char input[8192];
    size_t nInput = fread(input, 1, sizeof(input) - 1, stdin);
    /* Trim trailing newline. */
    if (nInput > 0 && input[nInput-1] == '\n') nInput--;
    input[nInput] = 0;

    printf("Input:  %zu bytes: ", nInput);
    for (size_t i = 0; i < nInput; i++) printf("%02X ", input[i]);
    printf("\n");

    int isNfc = utf_nfc_is_nfc(input, nInput);
    printf("NFC:    %s\n", isNfc ? "yes" : "no");

    if (!isNfc) {
        /* NFC can expand: sizing at utf_nfc_normalize_bound() rules out
         * truncation.  The status is still checked — a segment limit is
         * reported the same way, and a short result is otherwise
         * indistinguishable from text that legitimately composed smaller. */
        unsigned char output[8192];
        size_t nOutput;

        if (utf_nfc_normalize_bound(nInput) > sizeof(output) - 1) {
            fprintf(stderr, "input too large to normalize safely\n");
            return 1;
        }

        utf_nfc_status rc = utf_nfc_normalize(input, nInput,
                                              output, sizeof(output) - 1,
                                              &nOutput);
        if (UTF_NFC_OK != rc) {
            fprintf(stderr, "normalize failed (%s); output would be truncated\n",
                    UTF_NFC_TRUNCATED == rc ? "buffer too small"
                                            : "combining sequence too long");
            return 1;
        }
        output[nOutput] = 0;

        printf("Normal: %zu bytes: ", nOutput);
        for (size_t i = 0; i < nOutput; i++) printf("%02X ", output[i]);
        printf("\n");
        printf("Text:   %s\n", output);
    }

    return 0;
}
