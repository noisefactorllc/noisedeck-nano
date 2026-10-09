#!/usr/bin/env bash
# Host-side check of the portable engine under ASan/UBSan with ASCII previews.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$(mktemp -d)"
# -no-pie: with kernel ASLR entropy above what ASan's fixed shadow mapping
# expects, PIE binaries intermittently segfault at startup (DEADLYSIGNAL);
# a fixed load address makes the sanitized build start reliably.
cc -std=c99 -O1 -g -no-pie -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra \
  -o "$OUT/test" "$ROOT/host/test.c" "$ROOT/noisedeck_nano/src/fx/fx.c" \
  "$ROOT/noisedeck_nano/src/fx/palette.c" "$ROOT/noisedeck_nano/src/fx/text.c" \
  "$ROOT/noisedeck_nano/src/ui/ui.c" -lm
"$OUT/test"
rm -rf "$OUT"
