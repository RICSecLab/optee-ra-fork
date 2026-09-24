#!/bin/sh
# Restore the verification/ directory from a console log that contains the
# hex block printed by ima-quote on the board.
#   sh host-unpack.sh <console-log> [<dest-dir>]
set -e
LOG="${1:?usage: $0 <console-log> [dest-dir]}"; DEST="${2:-.}"
mkdir -p "$DEST"
# strip terminal timestamps and CRs, then take the block between the markers
sed 's/^\[[^]]*\] //; s/\r$//' "$LOG" \
  | sed -n '/^-----BEGIN VERIFICATION HEX-----/,/^-----END VERIFICATION HEX-----/p' \
  | sed '1d;$d' | tr -d ' \n' | xxd -r -p | tar xzf - -C "$DEST"
ls -d "$DEST/verification"
