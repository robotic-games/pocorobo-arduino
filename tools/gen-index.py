#!/usr/bin/env python3
"""ボードマネージャ用の package_pocorobo_index.json を作る。

使い方: tools/gen-index.py --version X.Y.Z --zip <platform zip> --zip-url <URL> --out <path>

ツールチェーン(コンパイラ・esptool 等)は自前で持たず、arduino-esp32 公式 index の
定義を packager 名だけ pocorobo に変えて写す(ダウンロード先は Espressif のまま)。
写すのは ESP32-S3 に要るもの(KEEP_TOOLS)だけ。
公式 index は tools/build-package.sh が build/ に置いたものを読む。
"""

import argparse
import hashlib
import json
import re
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PACKAGER = "pocorobo"
SITE_URL = "https://github.com/robotic-games/pocorobo-arduino"

# 公式の toolsDependencies のうち残すツール。ESP32-S3 のビルド・書き込み・デバッグに要るものだけ。
# 根拠は公式 platform.txt が esp32s3 向けのレシピで参照する runtime.tools.*.path
# (build.tarch=xtensa、build.chip_variant=build.mcu=esp32s3 で展開したもの):
#   esp-x32             compiler.path={tools.{build.tarch}-esp-elf-gcc.path} → runtime.tools.esp-x32.path
#   esp32s3-libs        tools.esp32-arduino-libs.path={runtime.tools.{build.chip_variant}-libs.path}
#   esptool_py          tools.esptool_py.path={runtime.tools.esptool_py.path}(書き込みとブートローダ生成)
#   dfu-util            tools.dfu-util.path={runtime.tools.dfu-util-0.11.0-arduino5.path}
#                       (packager は arduino。Arduino 公式 index が配る)
#   xtensa-esp-elf-gdb  debug.toolchain.path={tools.{build.tarch}-esp-elf-gdb.path}
#   openocd-esp32       debug.server.openocd.path={runtime.tools.openocd-esp32.path}
#   mkspiffs/mklittlefs platform.txt は参照しないが、SPIFFS / LittleFS のデータ書き込みプラグインが
#                       runtime.tools.<名前>.path を探す。各 0.1 MB
# 落とすもの: esp-rv32 と riscv32-esp-elf-gdb(RISC-V 系チップ用)、esp32s3-libs 以外の *-libs
KEEP_TOOLS = {
    "esp-x32",
    "esp32s3-libs",
    "esptool_py",
    "dfu-util",
    "xtensa-esp-elf-gdb",
    "openocd-esp32",
    "mkspiffs",
    "mklittlefs",
}


def find_core_platform(index, version):
    for pkg in index["packages"]:
        if pkg["name"] != "esp32":
            continue
        for platform in pkg["platforms"]:
            if platform["architecture"] == "esp32" and platform["version"] == version:
                return pkg, platform
    sys.exit(f"公式 index に esp32 {version} がありません")


def read_from_zip(zip_path):
    """zip 内の platform.txt の version と boards.txt のボード名を返す。"""
    with zipfile.ZipFile(zip_path) as z:
        tops = {n.split("/", 1)[0] for n in z.namelist()}
        if len(tops) != 1:
            sys.exit(f"zip のトップディレクトリが 1 つではありません: {sorted(tops)}")
        top = tops.pop()
        platform_txt = z.read(f"{top}/platform.txt").decode("utf-8")
        boards_txt = z.read(f"{top}/boards.txt").decode("utf-8")
    version = re.search(r"^version=(.*)$", platform_txt, re.M).group(1).strip()
    boards = [m.group(1) for m in re.finditer(r"^[^.#\s]+\.name=(.*)$", boards_txt, re.M)]
    return version, boards


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version", required=True, help="パッケージの版")
    parser.add_argument("--zip", required=True, type=Path, help="build-package.sh が作った zip")
    parser.add_argument("--zip-url", required=True, help="zip の配信 URL")
    parser.add_argument("--out", required=True, type=Path, help="出力先")
    parser.add_argument(
        "--esp32-index",
        type=Path,
        default=REPO / "build" / "package_esp32_index.json",
        help="arduino-esp32 公式 index(既定: build-package.sh が置いたもの)",
    )
    args = parser.parse_args()

    if not args.esp32_index.is_file():
        sys.exit(f"{args.esp32_index} がありません。先に tools/build-package.sh を実行してください")
    core_version = (REPO / "tools" / "esp32-core-version.txt").read_text().strip()
    core_pkg, core_platform = find_core_platform(json.loads(args.esp32_index.read_text()), core_version)

    zip_version, boards = read_from_zip(args.zip)
    if zip_version != args.version:
        sys.exit(f"--version {args.version} と zip 内の platform.txt の version={zip_version} が違います")
    if not boards:
        sys.exit("zip 内の boards.txt にボードがありません")
    if not args.zip_url.endswith("/" + args.zip.name):
        sys.exit(f"--zip-url の末尾が zip のファイル名 {args.zip.name} ではありません: {args.zip_url}")

    # toolsDependencies は公式のうち KEEP_TOOLS だけ写す。公式 index が定義しているツール
    # (packager=esp32)は packager を pocorobo に付け替えて tools にも定義を写す。
    # 他(arduino:dfu-util)は Arduino 公式 index が配るものなので packager をそのまま残す
    core_tools = {(t["name"], t["version"]): t for t in core_pkg["tools"]}
    deps, tools, dropped = [], [], []
    for dep in core_platform["toolsDependencies"]:
        dep = dict(dep)
        if dep["name"] not in KEEP_TOOLS:
            dropped.append(dep["name"])
            continue
        if dep["packager"] == core_pkg["name"]:
            key = (dep["name"], dep["version"])
            if key not in core_tools:
                sys.exit(f"公式 index に tools の定義がありません: {key}")
            dep["packager"] = PACKAGER
            if not any((t["name"], t["version"]) == key for t in tools):
                tools.append(core_tools[key])
        deps.append(dep)
    missing = sorted(KEEP_TOOLS - {d["name"] for d in deps})
    if missing:
        sys.exit(f"公式の toolsDependencies に無いツールです(名前が変わった?): {missing}")

    data = args.zip.read_bytes()
    index = {
        "packages": [
            {
                "name": PACKAGER,
                "maintainer": "Robotic Sports Games, Co., Ltd.",
                "websiteURL": SITE_URL,
                "help": {"online": SITE_URL},
                "platforms": [
                    {
                        "name": "Pocorobo ESP32 Boards",
                        "architecture": "esp32",
                        "version": args.version,
                        "category": "Contributed",
                        "url": args.zip_url,
                        "archiveFileName": args.zip.name,
                        "checksum": "SHA-256:" + hashlib.sha256(data).hexdigest(),
                        "size": str(len(data)),
                        "boards": [{"name": b} for b in boards],
                        "help": {"online": SITE_URL},
                        "toolsDependencies": deps,
                    }
                ],
                "tools": tools,
            }
        ]
    }
    args.out.write_text(json.dumps(index, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"{args.out}: platform {args.version} ({len(boards)} boards), {len(tools)} tools from esp32 {core_version}")
    print(f"  kept:    {', '.join(d['packager'] + ':' + d['name'] for d in deps)}")
    print(f"  dropped: {', '.join(dropped)}")


if __name__ == "__main__":
    main()
