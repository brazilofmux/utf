/*
 * nearest_color.c — Find nearest xterm-256 palette color for an RGB value.
 *
 * Demonstrates CIE97 perceptual distance with Kd-tree search.
 *
 * Build: gcc -O2 -I../include -o nearest_color nearest_color.c -L.. -lutf -lm
 * Usage: ./nearest_color 255 128 0
 */

#include <stdio.h>
#include <stdlib.h>
#include "utf/cie97.h"

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s R G B\n", argv[0]);
        return 1;
    }

    unsigned char rgb[3];
    rgb[0] = (unsigned char)atoi(argv[1]);
    rgb[1] = (unsigned char)atoi(argv[2]);
    rgb[2] = (unsigned char)atoi(argv[3]);

    int idx256 = co_nearest_xterm256(rgb);
    int idx16  = co_nearest_xterm16(rgb);

    printf("Input:         RGB(%d, %d, %d)\n", rgb[0], rgb[1], rgb[2]);
    printf("Nearest-256:   index %d  ->  RGB(%d, %d, %d)\n",
           idx256, palette[idx256].rgb.r, palette[idx256].rgb.g, palette[idx256].rgb.b);
    printf("Nearest-16:    index %d  ->  RGB(%d, %d, %d)\n",
           idx16, palette[idx16].rgb.r, palette[idx16].rgb.g, palette[idx16].rgb.b);

    /* Show the color in the terminal. */
    printf("Preview-256:   \033[48;5;%dm       \033[0m\n", idx256);
    printf("Preview-16:    \033[4%dm       \033[0m\n", idx16 < 8 ? idx16 : idx16 - 8 + 100);

    return 0;
}
