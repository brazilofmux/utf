/*
 * sort_lines.c — Sort lines from stdin using DUCET collation.
 *
 * Demonstrates utf_collate_cmp() for linguistically correct ordering.
 *
 * Build: gcc -O2 -I../include -o sort_lines sort_lines.c -L.. -lutf -lm
 * Usage: echo -e "café\ncafe\nCafé\nnaïve\nnaive" | ./sort_lines
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utf/collate.h"

#define MAX_LINES 10000
#define MAX_LINE  4096

static unsigned char *lines[MAX_LINES];
static size_t lens[MAX_LINES];
static int nLines;

static int cmp_fn(const void *a, const void *b)
{
    int i = *(const int *)a;
    int j = *(const int *)b;
    return utf_collate_cmp(lines[i], lens[i], lines[j], lens[j]);
}

int main(void)
{
    char buf[MAX_LINE];
    nLines = 0;

    while (nLines < MAX_LINES && fgets(buf, sizeof(buf), stdin)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') len--;
        lines[nLines] = (unsigned char *)malloc(len);
        memcpy(lines[nLines], buf, len);
        lens[nLines] = len;
        nLines++;
    }

    int indices[MAX_LINES];
    for (int i = 0; i < nLines; i++) indices[i] = i;
    qsort(indices, nLines, sizeof(int), cmp_fn);

    for (int i = 0; i < nLines; i++) {
        fwrite(lines[indices[i]], 1, lens[indices[i]], stdout);
        putchar('\n');
    }

    for (int i = 0; i < nLines; i++) free(lines[i]);
    return 0;
}
