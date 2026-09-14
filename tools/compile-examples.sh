#!/usr/bin/env bash
# スケッチ例を arduino-cli でコンパイルする
#
# 使い方: tools/compile-examples.sh [standard|mini]   (引数省略で両方)
#   arduino-cli のパスは環境変数 ARDUINO_CLI で差し替えられる(既定: arduino-cli)
#
# ボードパッケージができるまでの暫定として、汎用の ESP32-S3 ボードに
# platform/variants/pocorobo_<board>/pins_arduino.h を -include で差し込んでコンパイルする。
set -euo pipefail

cli="${ARDUINO_CLI:-arduino-cli}"
repo="$(cd "$(dirname "$0")/.." && pwd)"
fqbn="esp32:esp32:esp32s3:CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=8M,PartitionScheme=default_8MB"

if [ $# -eq 0 ]; then
  set -- standard mini
fi
for board in "$@"; do
  case "$board" in
    standard | mini) ;;
    *)
      echo "unknown board: $board (standard|mini)" >&2
      exit 2
      ;;
  esac
done

failed=""
for board in "$@"; do
  pins="$repo/platform/variants/pocorobo_$board/pins_arduino.h"
  for sketch in "$repo"/libraries/Pocorobo/examples/*/; do
    sketch="${sketch%/}"
    echo "==== $board: $(basename "$sketch")"
    if ! "$cli" compile \
      --fqbn "$fqbn" \
      --libraries "$repo/libraries" \
      --build-property "compiler.cpp.extra_flags=-include \"$pins\"" \
      --build-property "compiler.c.extra_flags=-include \"$pins\"" \
      --warnings all \
      "$sketch"; then
      failed+="  $board: $(basename "$sketch")"$'\n'
    fi
  done
done

if [ -n "$failed" ]; then
  echo "==== FAILED:" >&2
  printf '%s' "$failed" >&2
  exit 1
fi
echo "==== all examples compiled"
