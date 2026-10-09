# Code Generation Pipeline

This directory contains the C++ tools and Perl and Python scripts that
generate the compressed DFA tables from Unicode data files.  **You do not need this
to use the library** — the pre-generated C tables in `../tables/` are
ready to compile.

Use this pipeline only when updating to a new Unicode version.

## Prerequisites

- C++ compiler (g++ or clang++)
- Perl 5
- Python 3 (gen_ducet.py)
- GNU Make / autoconf (optional, for `Makefile.in`)

## Pipeline Overview

```
Unicode data files (data/)
        |
        v
  Perl and Python scripts (gen_*.pl, gen_ducet.py)
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

## Scripts

| Script | Input | Output |
|--------|-------|--------|
| gen_ccc.pl | UnicodeData.txt | data/tr_ccc.txt |
| gen_compose.pl | UnicodeData.txt, CompositionExclusions.txt | data/tr_compose.txt |
| gen_ducet.py | allkeys.txt, UnicodeData.txt, Scripts.txt, cldr/collation/*.xml | ../tables/ducet_dfa_tables.c, ../tables/ducet_cetable.c, the DUCET section of ../include/utf/utf_tables.h; data/tr_ducet*.txt (root only, for the C++ builders) |
| gen_extpict.pl | emoji-data.txt | data/cl_ExtPict.txt |
| gen_gcb.pl | GraphemeBreakProperty.txt | data/tr_gcb.txt |
| gen_word.pl | DerivedCoreProperties.txt, UnicodeData.txt | data/cl_Word.txt |
| gen_nfcqc.pl | DerivedNormalizationProps.txt | data/tr_nfcqc.txt |
| gen_nfd.pl | UnicodeData.txt | data/tr_nfd.txt |

## Unicode Data Files (data/)

Downloaded from https://www.unicode.org/Public/16.0.0/ucd/:

- `UnicodeData.txt` — Master character database
- `allkeys.txt` — DUCET collation element table
- `DerivedNormalizationProps.txt` — Normalization properties
- `EastAsianWidth.txt` — East Asian Width
- `GraphemeBreakProperty.txt` — Grapheme cluster break
- `DerivedCoreProperties.txt` — Alphabetic (for the word-character set)
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

## Collation and locales

`gen_ducet.py` builds every collator from DUCET and the CLDR 46 rules in
`data/cldr/collation/` (Unicode license in `data/cldr/LICENSE`), using
`cldr_tailor.py` to apply the rules and `dfa_pool.py` to build the DFAs.
All collators share one pool of DFA states, so a locale costs only the
rows its tailoring changes.  The locales are listed in `LOCALES` at the
top of `gen_ducet.py`; adding one means copying its XML file here.

Consumers of the collators that also need CLDR 46's locale data for the
same 53 locales (dates, numbers, currencies) get it from
`data/cldr/main/` and `data/cldr/supplemental/`.  Nothing in libutf
reads those files and they are about 28 MB, so they are not committed:
`fetch_cldr.py` downloads them from the `release-46` tag of
unicode-org/cldr (or symlinks them from a local checkout given by
`--mirror DIR` or `$CLDR_MIRROR`) and checks every file against the
committed `data/cldr/SHA256SUMS`.  Re-running it is a no-op once the
cache is complete; `--verify` only checks.  Rewrite the manifest with
`--update-manifest` when moving to a new CLDR release.

The generator checks its own output (every root entry and every entry a
locale changed is read back through the emitted DFAs) and is
deterministic.  Regenerating changes the table fingerprints in
`tests/test_color_ops.c` -- see `CLAUDE.md` before updating them.

