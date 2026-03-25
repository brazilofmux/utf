# Beating ICU at Unicode with a 600 KB Library

ICU is the gold standard for Unicode processing. It ships inside
Chrome, Android, Node.js, and most Linux distributions. It is
correct, comprehensive, and battle-tested.

It is also 42 megabytes.

I spent the last sixteen years building a different kind of Unicode
library—one designed around compressed deterministic finite automata
that consume UTF-8 bytes directly. No UTF-16 conversion. No malloc.
No dependencies. The whole thing compiles to a 592 KB static archive.

As of this week, it beats ICU on every UTF-8 benchmark I can throw
at it.

## The numbers

Benchmarks run on the same machine, same test data, same compiler
flags. ICU 74.2. All times are wall-clock milliseconds for
identical workloads.

### UTF-8 input (the common case)

| Operation | libutf | ICU 74.2 | Ratio |
|-----------|--------|----------|-------|
| NFC normalization | 84 ms | 119 ms | **1.4x faster** |
| NFC quick-check | 183 ms | 387 ms | **2.1x faster** |
| DUCET collation | 80 ms | 98 ms | **1.2x faster** |
| Grapheme segmentation | 560 ms | 770 ms | **1.4x faster** |

Four operations, four wins. The collation result is the one I'm
most proud of, because two weeks ago ICU was winning that benchmark.

### Core-to-core (each library in its native encoding)

| Operation | libutf (UTF-8) | ICU (UTF-16) | Ratio |
|-----------|----------------|--------------|-------|
| NFC normalization | 84 ms | 92 ms | **libutf 1.1x faster** |
| DUCET collation | 80 ms | 56 ms | ICU 1.4x faster |
| Grapheme segmentation | 560 ms | 690 ms | **libutf 1.2x faster** |

When each library operates in its native encoding—libutf on UTF-8,
ICU on UTF-16—NFC normalization and grapheme segmentation are
still faster in libutf. Collation is the one remaining category
where ICU's UTF-16 encoding gives it an inherent advantage: direct
array indexing into collation tables versus byte-level DFA traversal.
But the gap has narrowed from 2.1x to 1.4x.

### Size

| | Size |
|---|---|
| **libutf.a** (stripped) | **592 KB** |
| libicuuc.a + libicui18n.a + libicudata.a | 42,326 KB |
| **Ratio** | **71x smaller** |

## Why UTF-8 input matters

Most real-world text is UTF-8. Web pages, JSON APIs, log files,
databases, file paths, terminal I/O—it's all UTF-8.

ICU's internal representation is UTF-16. Every operation on UTF-8
input begins with a conversion pass: allocate a buffer, decode each
UTF-8 sequence into one or two 16-bit code units, then process the
result. For short strings this conversion can cost more than the
operation itself.

libutf skips that step entirely. Its DFAs consume the raw UTF-8
byte stream—one byte in, one state transition out. There is no
intermediate representation.

## How compressed DFAs work

The core design pattern is a deterministic finite automaton per
operation. Character classification, case mapping, normalization
properties, collation element lookup, grapheme break detection, and
character width are all DFA-driven.

A naive DFA for Unicode would need a 256-wide transition table at
every state (one entry per possible byte value), which gets large
fast. The compression pipeline uses three techniques:

1. **State merging.**  States with identical outgoing transitions
   collapse into one.

2. **Column deduplication.**  The 256-column transition table is
   scanned for duplicate columns, and a remapping layer is inserted
   so that identical columns share storage.

3. **RUN/COPY phrase encoding.**  The remaining table entries are
   compressed with a simple scheme: runs of repeated values and
   copies of literal sequences.

The result is tables small enough to fit in L2 cache. The collation
element table—mapping every Unicode code point to its DUCET
collation weights—compresses to 340 KB. For comparison, ICU's
equivalent data file is over 30 MB.

## The collation story

DUCET collation (Unicode Technical Standard #10) is the hardest
operation to make fast. You're comparing strings at up to four
levels—base character, accent, case, and tiebreaker—and each
code point can expand to multiple collation elements.

Two weeks ago, libutf's collation was slower than ICU on UTF-8 input.
ICU was 1.2x faster. Core-to-core, ICU was 2.1x faster. The DFA
approach was paying a real cost for byte-level traversal versus ICU's
direct array indexing on 16-bit code units.

Six commits changed that:

- **Latin fast path.**  For the common case of strings in
  U+0000..U+017F (ASCII + Latin Extended-A), weights are compared
  inline without DFA traversal or collation element array allocation.
  This covers English, French, German, Spanish, Portuguese, and most
  European languages.

- **Bounded-input path.**  For short non-Latin strings that fit in
  a fixed-size buffer, the full DFA lookup runs but avoids dynamic
  allocation.

- **Long-string overflow.**  Strings that exceed the bounded buffer
  fall back to a segmented approach that processes chunks
  incrementally.

- **CJK implicit weights.**  Han ideographs use algorithmically
  computed weights rather than table lookup, matching the UCA
  specification and avoiding a table that would otherwise be
  enormous.

- **Case-insensitive comparison.**  A new `utf_collate_cmp_ci`
  function skips level-3 (case) weights, which is both faster and
  what most applications actually want for search and deduplication.

The result: collation dropped from 120 ms to 80 ms, a 33% speedup.
On UTF-8 input, libutf is now 1.2x faster than ICU.

## The grapheme story

Grapheme cluster segmentation—finding the boundaries between
user-perceived characters—is 1.4x faster on UTF-8 input and 1.2x
faster core-to-core.

ICU's `UBreakIterator` is a general-purpose rule-based engine that
handles word breaks, sentence breaks, and line breaks in addition to
grapheme clusters. It operates on UTF-16 and interprets compiled
rule tables at runtime.

libutf's grapheme segmenter is a single-pass DFA. The Unicode
Grapheme Cluster Break rules (UAX #29) are compiled directly into a
state machine that reads UTF-8 bytes. One byte in, one state
transition, one output. No conversion, no rule interpretation, no
abstraction layers.

The margin here is narrower than for NFC or NFC quick-check because
grapheme segmentation is inherently cheap—the per-character work
is minimal in both implementations, so the DFA's byte-level advantage
doesn't compound as much as it does in normalization where each code
point triggers property lookups, decomposition, and recomposition.

## Color as Unicode

The library's origin as a MUD server component gave it an unusual
capability: inline color encoding in UTF-8 strings.

Most terminal applications handle color by interleaving ANSI SGR
escape sequences (`ESC[38;5;196m`) with text. This works for
output, but it makes string processing painful. Want to uppercase
a colored string? You have to parse and skip the escape sequences.
Want to reverse it? You have to track which escape sequences apply
to which characters. Want to measure its display width? You have
to ignore the escapes. Every string operation needs an
ANSI-aware variant.

libutf takes a different approach. Colors are encoded as Unicode
Private Use Area code points directly in the UTF-8 stream:

- **BMP PUA** (3-byte UTF-8, U+F500–F7FF): reset, bold, underline,
  blink, inverse, 256 foreground colors, 256 background colors
- **Supplementary PUA** (4-byte UTF-8, Plane 15): 24-bit RGB
  foreground and background, encoded as pairs of code points

A colored string is just a UTF-8 string. It's valid Unicode. It
round-trips through any UTF-8-aware tool without corruption. And
because the color codes are code points—not escape sequences—the
same byte-level state machine that processes the text can recognize
and skip the color codes in the same pass.

This is what makes the 60+ `co_*` functions possible. `co_toupper`
doesn't need an "ANSI-aware uppercase" implementation. It runs the
same Ragel state machine over the same UTF-8 byte stream, and the
machine's grammar already distinguishes color code points from
visible code points. One pass, zero special cases.

## Ragel state machines

The `co_*` functions—61 exported operations covering string
manipulation, word extraction, set operations, rendering, and
grapheme clustering—are generated by
[Ragel](https://www.colm.net/open-source/ragel/) from a single
4,500-line `.rl` source file.

Ragel compiles regular-expression grammars into C code. The `-G2`
backend produces goto-driven state machines: no tables, no
interpreter, just computed gotos. The compiler's optimization
passes eliminate dead states and minimize transitions.

The key insight is a shared grammar called `color_scan` that every
machine includes:

```ragel
machine color_scan;

# BMP PUA color: U+F500–F7FF → EF (94–9F) (80–BF)
color_bmp = 0xEF (0x94..0x9F) (0x80..0xBF);

# SMP PUA color: U+F0000–F3FFF → F3 (B0–B3) (80–BF) (80–BF)
color_smp = 0xF3 (0xB0..0xB3) (0x80..0xBF) (0x80..0xBF);

color   = color_bmp | color_smp;
visible = vis_1 | vis_2 | vis_3 | vis_4;   # everything else
```

Every `co_*` machine is then a one-liner over this grammar.
`co_visible_length` is:

```ragel
main := ( color | visible @count )*;
```

`co_toupper` is:

```ragel
main := ( color >mark @emit_color | visible >mark @emit_upper )*;
```

The pattern is always the same: match `color` or `visible`, attach
an action to each. Ragel compiles it into a single-pass,
branch-free state machine that handles every UTF-8 byte pattern
and every PUA color encoding in one traversal. No separate "is
this a color code?" check, no state tracking between functions.

The 4,500 lines of Ragel source expand to 6,200 lines of generated
C. Each function is a self-contained `switch`/`goto` block with
zero runtime dependencies.

## Perceptual color distance

When rendering 24-bit RGB colors on terminals that only support
256 or 16 colors, you need to find the nearest palette entry.
The naive approach—Euclidean distance in RGB space—gives bad
results because RGB space is not perceptually uniform. A
numerically small difference between two blues can be invisible,
while the same distance between a green and a yellow is obvious.

libutf converts to CIELAB color space (sRGB—linear RGB—XYZ
D65—CIELAB) and searches a pre-built Kd-tree over the xterm-256
palette. The Kd-tree cycles through L\*, a\*, b\* axes at each
level, pruning branches whose bounding boxes are farther than the
current best match. The result is perceptually accurate
nearest-color lookup in O(log n) time instead of O(n) linear scan.

This matters for the rendering pipeline. When `co_render_ansi256`
encounters a 24-bit RGB foreground color, it calls
`co_nearest_xterm256` to find the closest palette entry, then
emits the corresponding `ESC[38;5;Nm` sequence. The same applies
to `co_render_ansi16` for the basic 16-color palette. The user
sees the best possible approximation of the intended color, not
the nearest numerical neighbor in the wrong color space.

## What this is and isn't

libutf is not a replacement for ICU. ICU handles locales, date
formatting, message formatting, bidirectional text, complex text
layout, and dozens of other things that libutf does not attempt.

libutf is a replacement for the specific Unicode operations that
show up in systems programming and terminal applications:
normalizing strings, comparing them in sort order, segmenting them
into grapheme clusters, mapping case, measuring display width, and
handling inline color. If your application processes UTF-8 text—and
most server-side and terminal applications do—libutf does the
Unicode parts faster, in less memory, with no dependencies. And
the PUA color encoding and Ragel-generated `co_*` operations give
you something ICU doesn't offer at all: a way to carry styled text
through string operations without losing the styling.

The library is verified against ICU as a reference implementation:
31 NFC test cases match ICU byte-for-byte, and 33 collation test
cases match ICU ordering exactly. An additional 20 collation cases
verify sort key consistency, case-insensitive comparison, long-string
handling, and malformed UTF-8 resilience.

## Origin

This code has been running in production since 2008, inside
[TinyMUX](https://github.com/brazilofmux/tinymux)—a MUD server
that needed full Unicode support in a single-threaded,
latency-sensitive environment. When your event loop budget is
measured in microseconds per player command, you can't afford to
link 42 MB of ICU.

I recently extracted the Unicode components into a standalone C
library. It's MIT licensed and available at
[github.com/brazilofmux/utf](https://github.com/brazilofmux/utf).

```
git clone https://github.com/brazilofmux/utf.git
cd utf && make && make test
```

Unicode 16.0. Zero malloc. 592 KB. Faster than ICU on every
UTF-8 operation.
