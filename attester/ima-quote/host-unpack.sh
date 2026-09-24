#!/bin/sh
# Extract the quote directory from a terminal log that contains the
# "BEGIN QUOTE TARBALL" block printed by device-ak-quote.sh.
#   sh host-unpack.sh <terminal-log> [<dest-dir>]
set -e
LOG="${1:?usage: $0 <terminal-log> [dest-dir]}"; DEST="${2:-.}"
mkdir -p "$DEST"
# strip terminal timestamps and CRs, then take the block between the markers
CLEAN=$(mktemp)
sed 's/^\[[^]]*\] //; s/\r$//' "$LOG" > "$CLEAN"
if grep -q -E -- '^-----BEGIN .* HEX-----' "$CLEAN"; then
    sed -n '/^-----BEGIN .* HEX-----/,/^-----END .* HEX-----/p' "$CLEAN" | sed '1d;$d' | tr -d ' \n' | xxd -r -p | tar xzf - -C "$DEST"
elif grep -q -- "-----BEGIN QUOTE TARBALL" "$CLEAN"; then
    sed -n '/^-----BEGIN QUOTE TARBALL/,/^-----END QUOTE TARBALL/p' "$CLEAN" | sed '1d;$d' | tr -d ' ' | base64 -d | tar xzf - -C "$DEST"
else
    echo "no quote block found in $LOG"; rm -f "$CLEAN"; exit 1
fi
rm -f "$CLEAN"
ls -d "$DEST"/quote-* "$DEST"/verification 2>/dev/null
