#!/usr/bin/env bash
# Host-side check of the portable engine under ASan/UBSan with ASCII previews.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$(mktemp -d)"
cc -std=c99 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra \
  -o "$OUT/test" "$ROOT/host/test.c" "$ROOT/noisedeck_nano/src/fx/fx.c" \
  "$ROOT/noisedeck_nano/src/fx/palette.c" "$ROOT/noisedeck_nano/src/fx/text.c" \
  "$ROOT/noisedeck_nano/src/ui/ui.c" -lm
"$OUT/test"
rm -rf "$OUT"
