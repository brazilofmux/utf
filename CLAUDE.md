# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
make              # build libutf.a
make test         # build and run all 499 tests
make examples     # build example programs in examples/
make ragel        # regenerate src/color_ops.c from src/color_ops.rl (requires Ragel)
make clean        # remove all build artifacts
```

Run tests with options: `./tests/test_color_ops [-v] [-f <function>] [-s <seed>]`

- `-v` verbose output, `-f` filter to a single function, `-s` set fuzz seed.

## Architecture

**libutf** is a zero-allocation, pure C (C11) Unicode processing library. All functions write to caller-provided buffers. All tables are `const`. Thread-safe by construction.

### Core processing model: compressed DFAs over raw UTF-8

The central design pattern is deterministic finite automata that consume UTF-8 bytes directly — one byte in, one state transition — with no intermediate codepoint decoding. This applies to case mapping, character classification, normalization properties, collation element lookup, grapheme break detection, and character width.

### PUA color encoding

Colors are encoded as Unicode Private Use Area codepoints inline in UTF-8 strings (BMP PUA 3-byte for indexed colors/attributes, Plane 15 SMP PUA 4-byte for 24-bit RGB). The 60+ `co_*` functions in `color_ops` are Ragel-generated state machines that handle these inline color codes transparently during string operations.

### Module structure

- **`src/color_ops.rl`** — Ragel source for all `co_*` string operations. `src/color_ops.c` is the generated output (do not hand-edit; regenerate with `make ragel`).
- **`src/{cie97,collate,grapheme,nfc,console_width,classify}.c`** — Hand-written implementations for each module.
- **`include/utf/`** — Public API headers.
- **`tables/`** — Pre-generated compressed DFA tables in C. These are large generated files; modify via the `gen/` pipeline, not by hand.
- **`gen/`** — Table generation pipeline (C++ DFA builders + Perl scripts + Unicode 16.0 data files). Only needed when updating Unicode version.

### Key build notes

- `src/color_ops.o` is compiled with `-Wno-implicit-fallthrough -Wno-unused-const-variable` because Ragel -G2 generates intentional fallthroughs.
- The library links with `-lm` (math library, needed by CIE97 color distance).
- There is one test file (`tests/test_color_ops.c`) containing all 499 tests with built-in fuzz testing.

### Table fingerprints

`table_content` and `table_behaviour` in the test suite pin the generated DFA tables and the code that reads them. Three layers, each catching what the others cannot:

1. `_Static_assert`s that each `<table>_sot` dimension equals its `<TABLE>_ACCEPTING_STATES_START`. Compile-time.
2. Content hashes of the raw table data — the only coverage for `tr_foldmatch`, `tr_cp437`, `tr_latin1`, `tr_latin2`, which are exported but which nothing in the library reads.
3. Behavioural hashes sweeping every code point through the public API, which catch *reader* drift (a correct table read incorrectly) as well as data drift.

**If you regenerate a table, these will fail. That is the point.** Confirm the change is intended, then update the constants from the reported values. Do not update them to make a red suite green without knowing which table changed and why — the tables have silently drifted three times before, and each time it corrupted results rather than failing.

Note that `src/color_ops.rl` redeclares the GCB arrays after including `utf_tables.h`. That redundancy is load-bearing: it makes a dimension mismatch a compile error. Do not "clean it up".
