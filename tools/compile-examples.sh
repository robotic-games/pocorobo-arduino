#!/usr/bin/env bash
# スケッチ例を arduino-cli でコンパイルする
#
# 使い方: tools/compile-examples.sh [standard|mini]   (引数省略で両方)
#   ボード pocorobo:esp32:pocorobo_<board> でコンパイルする。ライブラリ Pocorobo は
#   platform に同梱されたものが使われる(--libraries は付けない)。
#   platform pocorobo:esp32 のインストールは呼び出し側の責任
#   (arduino-cli core install pocorobo:esp32 --additional-urls $POCOROBO_INDEX_URL)。
#
#   環境変数
#     ARDUINO_CLI        arduino-cli のパス(既定: arduino-cli)
#     POCOROBO_INDEX_URL --additional-urls に渡す index の URL
#                        (既定: https://robotic-games.github.io/pocorobo-arduino/package_pocorobo_index.json)
set -euo pipefail

cli="${ARDUINO_CLI:-arduino-cli}"
index_url="${POCOROBO_INDEX_URL:-https://robotic-games.github.io/pocorobo-arduino/package_pocorobo_index.json}"
repo="$(cd "$(dirname "$0")/.." && pwd)"

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
  for sketch in "$repo"/libraries/Pocorobo/examples/*/; do
    sketch="${sketch%/}"
    echo "==== $board: $(basename "$sketch")"
    if ! "$cli" compile \
      --fqbn "pocorobo:esp32:pocorobo_$board" \
      --additional-urls "$index_url" \
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
