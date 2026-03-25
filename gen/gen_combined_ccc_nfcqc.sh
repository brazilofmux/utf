#!/bin/bash
# gen_combined_ccc_nfcqc.sh — Generate the combined CCC+NFCQC DFA table
# and splice it into tables/nfc_tables.c and include/utf/utf_tables.h.
#
# Run from the repository root:
#   bash gen/gen_combined_ccc_nfcqc.sh
#
# Prerequisites: gen/data/tr_ccc.txt and gen/data/tr_nfcqc.txt must exist
# (produced by gen_ccc.pl and gen_nfcqc.pl from Unicode data).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GEN_DIR="$REPO_ROOT/gen"
DATA_DIR="$GEN_DIR/data"
TABLES_C="$REPO_ROOT/tables/nfc_tables.c"
TABLES_H="$REPO_ROOT/include/utf/utf_tables.h"

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# --- Step 1: Build the integers tool if needed ---
if [ ! -x "$GEN_DIR/integers" ]; then
    echo "Building integers tool..." >&2
    (cd "$GEN_DIR" && g++ -O3 -g -o integers integers.cpp ConvertUTF.cpp smutil.cpp)
fi

# --- Step 2: Generate combined data file ---
echo "Generating tr_ccc_nfcqc.txt..." >&2
(cd "$DATA_DIR" && perl "$GEN_DIR/gen_ccc_nfcqc.pl" > tr_ccc_nfcqc.txt) 2>&1

# --- Step 3: Run integers to produce DFA tables ---
echo "Building combined DFA..." >&2
: > "$TMPDIR/body.c"
: > "$TMPDIR/hdr.h"
(cd "$DATA_DIR" && "$GEN_DIR/integers" -o "$TMPDIR/body.c" -i "$TMPDIR/hdr.h" -d 0 \
    tr_ccc_nfcqc tr_ccc_nfcqc.txt) 2>&1

# --- Step 4: Extract defines and sizes from generated header ---
START_STATE=$(grep '#define TR_CCC_NFCQC_START_STATE' "$TMPDIR/hdr.h" | awk '{print $3}')
ACCEPT_START=$(grep '#define TR_CCC_NFCQC_ACCEPTING_STATES_START' "$TMPDIR/hdr.h" | awk '{print $3}')
ITT_SIZE=$(grep 'tr_ccc_nfcqc_itt' "$TMPDIR/hdr.h" | grep -o '\[[0-9]*\]' | tr -d '[]')
SOT_SIZE=$(grep 'tr_ccc_nfcqc_sot' "$TMPDIR/hdr.h" | grep -o '\[[0-9]*\]' | tr -d '[]')
SBT_SIZE=$(grep 'tr_ccc_nfcqc_sbt' "$TMPDIR/hdr.h" | grep -o '\[[0-9]*\]' | tr -d '[]')

echo "  States: ${ACCEPT_START//[()]/}, ITT[$ITT_SIZE], SOT[$SOT_SIZE], SBT[$SBT_SIZE]" >&2

# --- Step 5: Write header block to temp file ---
cat > "$TMPDIR/hdr_block.txt" <<EOF
/* tr_ccc_nfcqc: Combined CCC + NFC_QC in one DFA.
 * Result encoding: value = ccc * 3 + nfcqc.
 * Decode: ccc = value / 3, nfcqc = value % 3.
 */
#define TR_CCC_NFCQC_START_STATE $START_STATE
#define TR_CCC_NFCQC_ACCEPTING_STATES_START $ACCEPT_START
extern UTF_API const unsigned char  tr_ccc_nfcqc_itt[$ITT_SIZE];
extern UTF_API const unsigned short tr_ccc_nfcqc_sot[$SOT_SIZE];
extern UTF_API const unsigned short tr_ccc_nfcqc_sbt[$SBT_SIZE];

EOF

# --- Step 6: Update include/utf/utf_tables.h ---
# Remove old tr_ccc, tr_nfcqc, and any existing tr_ccc_nfcqc blocks,
# then insert combined block before tr_nfd marker.
awk '
    /^\/\* tr_ccc: Canonical/ { skip=1 }
    /^\/\* tr_nfcqc: NFC Quick/ { skip=1 }
    /^\/\* tr_ccc_nfcqc:/ { skip=1 }
    skip && /^$/ { skip=0; next }
    skip { next }
    /^\/\* tr_nfd: NFD decomposition/ {
        while ((getline line < "'"$TMPDIR/hdr_block.txt"'") > 0) print line
    }
    { print }
' "$TABLES_H" > "$TMPDIR/utf_tables.h"
cp "$TMPDIR/utf_tables.h" "$TABLES_H"
echo "Updated $TABLES_H" >&2

# --- Step 7: Update tables/nfc_tables.c ---
# Remove old tr_ccc, tr_nfcqc, and any existing tr_ccc_nfcqc block,
# then insert combined block before tr_nfd_itt.
awk '
    /^const unsigned char tr_ccc_itt/ { skip=1 }
    /^\/\/ utf\/tr_ccc_nfcqc/ { skip=1 }
    skip && /^const unsigned char tr_nfd_itt/ { skip=0 }
    skip { next }
    /^const unsigned char tr_nfd_itt/ {
        while ((getline line < "'"$TMPDIR/body.c"'") > 0) print line
    }
    { print }
' "$TABLES_C" > "$TMPDIR/nfc_tables.c"
cp "$TMPDIR/nfc_tables.c" "$TABLES_C"
echo "Updated $TABLES_C" >&2

echo "Done." >&2
