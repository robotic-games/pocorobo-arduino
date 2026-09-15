#!/usr/bin/env bash
# ボードマネージャ用の platform zip を作る
#
# 使い方: tools/build-package.sh [--version X.Y.Z] [--out <dir>]
#   arduino-esp32 公式の core(版は tools/esp32-core-version.txt)をダウンロードし、
#   boards.txt / variants / パーティション表を Pocorobo のものに差し替え、
#   ライブラリ Pocorobo を同梱して pocorobo-esp32-<version>.zip を --out に出力する。
#   ダウンロードは build/ にキャッシュし、SHA-256 を公式 index の値と照合する。
#   展開した公式 core は build/esp32-core-<版>/ に残す(make-boards-txt.py が読む)。
set -euo pipefail

for cmd in curl unzip zip sha256sum python3; do
  command -v "$cmd" >/dev/null || { echo "$cmd が見つかりません" >&2; exit 1; }
done

repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$repo/build"
version="0.0.0"
out="$build/dist"
while [ $# -gt 0 ]; do
  case "$1" in
    --version) version="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    *) echo "不明な引数: $1 (--version X.Y.Z / --out <dir>)" >&2; exit 2 ;;
  esac
done

core_version="$(tr -d '[:space:]' < "$repo/tools/esp32-core-version.txt")"
core_index_url="https://espressif.github.io/arduino-esp32/package_esp32_index.json"
core_index="$build/package_esp32_index.json"
mkdir -p "$build" "$out"
out="$(cd "$out" && pwd)"

# 公式 index から core zip の URL と SHA-256 を読む(ファイル名を決め打ちしない)
if [ ! -f "$core_index" ]; then
  echo "==== 公式 index をダウンロード: $core_index_url"
  curl -fsSL -o "$core_index.part" "$core_index_url" && mv "$core_index.part" "$core_index"
fi
read -r core_url core_sha < <(python3 - "$core_index" "$core_version" <<'EOF'
import json, sys
index, version = json.load(open(sys.argv[1])), sys.argv[2]
for pkg in index["packages"]:
    if pkg["name"] != "esp32":
        continue
    for p in pkg["platforms"]:
        if p["architecture"] == "esp32" and p["version"] == version:
            print(p["url"], p["checksum"].removeprefix("SHA-256:"))
            sys.exit(0)
sys.exit(f"公式 index に esp32 {version} がありません")
EOF
)
core_zip="$build/$(basename "$core_url")"

# core zip をダウンロード(キャッシュ)して SHA-256 を照合する
if [ ! -f "$core_zip" ]; then
  echo "==== 公式 core をダウンロード: $core_url"
  curl -fsSL -o "$core_zip.part" "$core_url" && mv "$core_zip.part" "$core_zip"
fi
echo "$core_sha  $core_zip" | sha256sum -c - >/dev/null \
  || { echo "公式 core の SHA-256 が index と一致しません: $core_zip" >&2; exit 1; }

# 展開する(zip のトップディレクトリは 1 つだけのはず)
core_dir="$build/esp32-core-$core_version"
rm -rf "$core_dir" "$build/unzip.tmp"
mkdir -p "$build/unzip.tmp"
unzip -q "$core_zip" -d "$build/unzip.tmp"
top="$(find "$build/unzip.tmp" -mindepth 1 -maxdepth 1)"
[ "$(printf '%s\n' "$top" | wc -l)" -eq 1 ] && [ -f "$top/platform.txt" ] \
  || { echo "公式 core zip の構成が想定と違います: $core_zip" >&2; exit 1; }
mv "$top" "$core_dir"
rmdir "$build/unzip.tmp"

# Pocorobo のパッケージを組み立てる
name="pocorobo-esp32-$version"
stage="$build/stage"
pkg="$stage/$name"
rm -rf "$stage"
mkdir -p "$stage"
cp -a "$core_dir" "$pkg"
cp "$repo/platform/boards.txt" "$pkg/boards.txt"
rm -rf "$pkg/variants"
cp -a "$repo/platform/variants" "$pkg/variants"
cp "$repo/platform/tools/partitions/pocorobo.csv" "$pkg/tools/partitions/pocorobo.csv"
cp -a "$repo/libraries/Pocorobo" "$pkg/libraries/Pocorobo"
grep -q '^name=' "$pkg/platform.txt" && grep -q '^version=' "$pkg/platform.txt" \
  || { echo "platform.txt に name= / version= がありません" >&2; exit 1; }
sed -i -e 's/^name=.*/name=Pocorobo ESP32 Boards/' -e "s/^version=.*/version=$version/" "$pkg/platform.txt"

zip_path="$out/$name.zip"
rm -f "$zip_path"
(cd "$stage" && zip -q -r -X "$zip_path" "$name")
rm -rf "$stage"

echo "==== $zip_path"
echo "SHA-256: $(sha256sum "$zip_path" | cut -d' ' -f1)"
echo "size:    $(wc -c < "$zip_path")"
