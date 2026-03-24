# Code Generation Pipeline

This directory contains the C++ tools and Perl scripts that generate the
compressed DFA tables from Unicode data files.  **You do not need this
to use the library** — the pre-generated C tables in `../tables/` are
ready to compile.

Use this pipeline only when updating to a new Unicode version.

## Prerequisites

- C++ compiler (g++ or clang++)
- Perl 5
- GNU Make / autoconf (optional, for `Makefile.in`)

## Pipeline Overview

```
Unicode data files (data/)
        |
        v
  Perl scripts (gen_*.pl)
        |
        v
  Intermediate .txt files (data/cl_*.txt, data/tr_*.txt)
        |
        v
  C++ DFA builders (buildFiles, classify, integers, strings, pairs)
        |
        v
  Generated C tables (../tables/*.c)
```

## C++ Tools

| Tool | Source | Purpose |
|------|--------|---------|
| buildFiles | buildFiles.cpp | Master Unicode data parser, generates classification tables |
| classify | classify.cpp | Builds binary classification DFAs (is_printable, is_alpha, etc.) |
| integers | integers.cpp | Builds code-point-to-integer DFAs (widths, CCC, NFC_QC, color) |
| strings | strings.cpp | Builds code-point-to-string DFAs (case mapping, NFD decomposition) |
| pairs | pairs.cpp | Builds two-code-point-to-result DFAs (NFC composition, DUCET contractions) |

All tools share `smutil.cpp/h` (state machine compression library) and
`ConvertUTF.cpp/h` (UTF-8/16/32 conversion).

## Perl Scripts

| Script | Input | Output |
|--------|-------|--------|
| gen_ccc.pl | UnicodeData.txt | data/tr_ccc.txt |
| gen_compose.pl | UnicodeData.txt, CompositionExclusions.txt | data/tr_compose.txt |
| gen_ducet.pl | allkeys.txt, DerivedNormalizationProps.txt | data/tr_ducet.txt, data/tr_ducet_contract.txt, ducet_cetable.h |
| gen_extpict.pl | emoji-data.txt | data/cl_ExtPict.txt |
| gen_gcb.pl | GraphemeBreakProperty.txt | data/tr_gcb.txt |
| gen_nfcqc.pl | DerivedNormalizationProps.txt | data/tr_nfcqc.txt |
| gen_nfd.pl | UnicodeData.txt | data/tr_nfd.txt |

## Unicode Data Files (data/)

Downloaded from https://www.unicode.org/Public/16.0.0/ucd/:

- `UnicodeData.txt` — Master character database
- `allkeys.txt` — DUCET collation element table
- `DerivedNormalizationProps.txt` — Normalization properties
- `EastAsianWidth.txt` — East Asian Width
- `GraphemeBreakProperty.txt` — Grapheme cluster break
- `CompositionExclusions.txt` — NFC composition exclusions
- `emoji-data.txt` — Extended pictographic property
- `SpecialCasing.txt` — Special case mappings
- `UnicodeHan.txt` — CJK Unified Ideographs data

## Updating to a New Unicode Version

1. Download new data files from unicode.org into `data/`.
2. Run the Perl scripts to regenerate intermediate `.txt` files.
3. Build the C++ tools and run them to regenerate C tables.
4. Copy the output to `../tables/`.
5. Rebuild the library: `cd .. && make clean && make && make test`.
