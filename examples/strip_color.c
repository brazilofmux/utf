/*
 * strip_color.c — Parse ANSI color from stdin, render to different targets.
 *
 * Demonstrates the PUA color pipeline:
 *   ANSI input -> co_parse_ansi -> PUA internal -> co_render_* -> output
 *
 * Build: gcc -O2 -I../include -o strip_color strip_color.c -L.. -lutf -lm
 * Usage: echo -e "\033[31mHello\033[0m World" | ./strip_color [ascii|ansi16|ansi256|truecolor]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utf/color_ops.h"

int main(int argc, char **argv)
{
    const char *mode = (argc > 1) ? argv[1] : "ascii";

    unsigned char input[UTF_BUFSIZE];
    size_t nInput = fread(input, 1, sizeof(input) - 1, stdin);
    input[nInput] = 0;

    /* Parse ANSI escape sequences into PUA color codes. */
    unsigned char pua[UTF_BUFSIZE];
    size_t nPua = co_parse_ansi(input, nInput, pua, sizeof(pua));

    unsigned char output[UTF_BUFSIZE];
    size_t nOutput = 0;

    if (0 == strcmp(mode, "ascii")) {
        nOutput = co_render_ascii(output, pua, nPua);
    } else if (0 == strcmp(mode, "ansi16")) {
        nOutput = co_render_ansi16(output, pua, nPua, 1);
    } else if (0 == strcmp(mode, "ansi256")) {
        nOutput = co_render_ansi256(output, pua, nPua, 1);
    } else if (0 == strcmp(mode, "truecolor")) {
        nOutput = co_render_truecolor(output, pua, nPua, 1);
    } else {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        fprintf(stderr, "Modes: ascii, ansi16, ansi256, truecolor\n");
        return 1;
    }

    fwrite(output, 1, nOutput, stdout);
    if (nOutput > 0 && output[nOutput-1] != '\n') putchar('\n');

    return 0;
}
