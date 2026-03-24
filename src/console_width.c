/*
 * console_width.c — Console width of a single visible code point.
 *
 * Uses the tr_widths DFA tables for Unicode 16.0 East Asian Width
 * and combining mark detection.
 *
 * Returns 0 for combining marks, 2 for fullwidth/CJK, 1 otherwise.
 */

#include "utf/utf_tables.h"

int co_console_width(const unsigned char *pCodePoint)
{
    const unsigned char *p = pCodePoint;
    int iState = TR_WIDTHS_START_STATE;
    do {
        unsigned char ch = *p++;
        unsigned char iColumn = tr_widths_itt[ch];
        unsigned short iOffset = tr_widths_sot[iState];
        for (;;) {
            int y = tr_widths_sbt[iOffset];
            if (y < 128) {
                if (iColumn < y) {
                    iState = tr_widths_sbt[iOffset + 1];
                    break;
                } else {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset += 2;
                }
            } else {
                y = 256 - y;
                if (iColumn < y) {
                    iState = tr_widths_sbt[iOffset + iColumn + 1];
                    break;
                } else {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset = (unsigned short)(iOffset + y + 1);
                }
            }
        }
    } while (iState < TR_WIDTHS_ACCEPTING_STATES_START);
    return (iState - TR_WIDTHS_ACCEPTING_STATES_START);
}
