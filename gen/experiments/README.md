# Locale collation experiment

Throwaway measurement (2026-10-08): what would CLDR collation tailorings
cost if every locale's DUCET DFAs lived in one shared state pool, a locale
being only a start state?  Nothing here feeds the build.

## Setup

CLDR 46 (Unicode 16.0) collation data, which `.gitignore` keeps out of the
tree:

```bash
curl -sSLO https://github.com/unicode-org/cldr/releases/download/release-46/cldr-common-46.0.zip
unzip -q cldr-common-46.0.zip 'common/collation/*' -d cldr
```

The ICU oracle needs ICU development headers:

```bash
gcc -O2 -o icukey icukey.c $(pkg-config --cflags --libs icu-i18n)
```

## Files

| File | Purpose |
|------|---------|
| smpy.py | Python replica of `../smutil.cpp` for concrete-default machines (no don't-cares), with rows hash-consed across machines |
| verify_root.py | Checks smpy against the shipped tables: 702 states / 206 columns / 43141 sbt, and 294 / 77 / 2813 for contractions |
| tailor.py | CLDR rule engine over `../data/allkeys.txt`: resets, relations, `[before n]`, `[first/last …]`, star lists, extensions, imports, parent locales, canonical closure, and respacing into one shared integer weight space |
| validate.py | Compares the engine's order with ICU's: `python3 validate.py [locale …]` |
| icukey.c | Prints ICU tertiary (or, with a second argument, secondary) sort keys |
| measure.py | Builds the pool and reports sizes: `python3 measure.py '{"group": ["sv", "da"]}'`, writing results.json |
| locales.txt | Every CLDR 46 collation file except root |

## Results

Base DFAs (singles plus contractions): 94,412 bytes.

| Set | DFA growth | New CEs | Weight slots p/s/t |
|-----|-----------:|--------:|-------------------:|
| 54 European | +45 KB | ~6 KB | 28138 / 282 / 79 |
| 128 non-CJK | +129 KB | ~31 KB | 29234 / 668 / 355 |
| all 132 | +1.2 MB | ~700 KB | 127012 / 8140 / 531 |

- European: fits 16-bit sbt.  Weights fit 32-bit CEs packed as 15-bit
  primary, 1 variable bit, 9-bit secondary, 7-bit tertiary (today's
  5-bit tertiary does not).
- Non-CJK: sbt passes 65535 (the sot goes 32-bit), and secondary plus
  tertiary need 19 bits.
- zh_Hant (535 KB) and zh (396 KB) dominate; tens of thousands of Han get
  explicit primaries.

Against ICU 72 (CLDR 42), 67 locales agree on every tested pair, every
European one among them.  The rest differ by CLDR data version (lv's long
vowels, zh's pinyin coverage), by ICU 72 lacking the locale, or by
`[reorder]`, which the engine records but does not apply.

Not modelled: `[reorder]` and the other settings (runtime parameters),
prefix rules (ja), contractions longer than three code points, and
quaternary relations.
