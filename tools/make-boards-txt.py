#!/usr/bin/env python3
"""arduino-esp32 公式の boards.txt から platform/boards.txt を生成する。

公式の esp32s3(ESP32S3 Dev Module)の定義を土台に、Pocorobo Standard / Mini の
2 ボードを作る。メニューは UploadSpeed / DebugLevel / EraseFlash だけ残し、
他のメニューは Pocorobo の基板に合う選択肢を固定キーとして展開する
(展開する値は公式ファイルから読む。ここに値を書き写さない)。

使い方: tools/make-boards-txt.py [--source <公式 boards.txt>] [--out <出力先>]
  --source 省略時は build/ に展開済みの公式 core、無ければ ~/.arduino15 の
  インストール済み core(いずれも tools/esp32-core-version.txt の版)を探す。
"""

import argparse
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BASE_BOARD = "esp32s3"

# 残すメニュー(利用者が Arduino IDE で選べるもの)
KEEP_MENUS = ["UploadSpeed", "DebugLevel", "EraseFlash"]

# 固定するメニュー: メニュー名 → 公式の選択肢 ID。選択肢のキーを固定キーとして展開する
FIXED_MENUS = {
    "PSRAM": "disabled",
    "FlashMode": "qio",
    "FlashSize": "8M",
    "LoopCore": "1",
    "EventsCore": "1",
    "USBMode": "hwcdc",
    "CDCOnBoot": "cdc",
    "MSCOnBoot": "default",
    "DFUOnBoot": "default",
    "UploadMode": "default",
    "CPUFreq": "240",
    "JTAGAdapter": "default",
    "ZigbeeMode": "default",
}

# メニューごと捨てるもの(下の OVERRIDES で Pocorobo 専用の値に置き換える)
DROP_MENUS = ["PartitionScheme"]

# UploadSpeed の既定(メニューの先頭に置く選択肢 ID)
DEFAULT_UPLOAD_SPEED = "921600"

# 全ボード共通で上書きする固定キー
OVERRIDES = {
    "build.flash_size": "8MB",
    "build.partitions": "pocorobo",
    "upload.maximum_size": "3145728",
}

# ボードごとの固定キー
BOARDS = {
    "pocorobo_standard": {
        "name": "Pocorobo Standard",
        "build.variant": "pocorobo_standard",
        "build.board": "POCOROBO_STANDARD",
    },
    "pocorobo_mini": {
        "name": "Pocorobo Mini",
        "build.variant": "pocorobo_mini",
        "build.board": "POCOROBO_MINI",
    },
}


def esp32_core_version():
    return (REPO / "tools" / "esp32-core-version.txt").read_text().strip()


def default_source():
    version = esp32_core_version()
    candidates = [
        REPO / "build" / f"esp32-core-{version}" / "boards.txt",
        Path.home() / ".arduino15" / "packages" / "esp32" / "hardware" / "esp32" / version / "boards.txt",
    ]
    for path in candidates:
        if path.is_file():
            return path
    sys.exit(
        "公式の boards.txt が見つかりません。--source で指定するか、"
        "tools/build-package.sh を先に実行してください。探した場所:\n  "
        + "\n  ".join(str(p) for p in candidates)
    )


def parse(path):
    """boards.txt を (キー, 値) の列で返す。空行・コメントは捨てる。"""
    entries = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, sep, value = line.partition("=")
        if not sep:
            sys.exit(f"'=' の無い行があります: {line}")
        entries.append((key, value))
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", type=Path, help="公式 boards.txt のパス")
    parser.add_argument("--out", type=Path, default=REPO / "platform" / "boards.txt", help="出力先")
    args = parser.parse_args()
    source = args.source or default_source()

    entries = parse(source)
    menu_labels = {k[len("menu."):]: v for k, v in entries if k.startswith("menu.")}
    prefix = BASE_BOARD + "."
    base = [(k[len(prefix):], v) for k, v in entries if k.startswith(prefix)]
    if not base:
        sys.exit(f"{source} に {BASE_BOARD} の定義がありません")

    # 固定キー(メニュー以外)は順序を保って辞書へ
    fixed = {}
    # メニュー: メニュー名 → 選択肢 ID → [(選択肢配下のキー, 値)]。キーが空のものが表示名
    menus = {}
    for key, value in base:
        if not key.startswith("menu."):
            fixed[key] = value
            continue
        _, menu, option, *rest = key.split(".", 3)
        menus.setdefault(menu, {}).setdefault(option, []).append((rest[0] if rest else "", value))

    unknown = sorted(set(menus) - set(KEEP_MENUS) - set(FIXED_MENUS) - set(DROP_MENUS))
    if unknown:
        sys.exit(f"扱いを決めていないメニューがあります(残す/固定/捨てるのどれかに追加する): {unknown}")
    missing = sorted((set(KEEP_MENUS) | set(FIXED_MENUS)) - set(menus))
    if missing:
        sys.exit(f"公式の {BASE_BOARD} に無いメニューです: {missing}")

    # 固定メニューの選択肢を固定キーとして展開する
    for menu, option in FIXED_MENUS.items():
        if option not in menus[menu]:
            sys.exit(f"メニュー {menu} に選択肢 {option} がありません: {sorted(menus[menu])}")
        for sub, value in menus[menu][option]:
            if not sub:
                continue
            if sub.split(".", 1)[0] in ("windows", "linux", "macosx"):
                sys.exit(f"メニュー {menu}.{option} に OS 別のキー {sub} があり固定キーに展開できません")
            fixed[sub] = value
    fixed.update(OVERRIDES)

    # UploadSpeed は既定の選択肢を先頭に置く(Arduino は先頭の選択肢を既定にする)
    speeds = menus["UploadSpeed"]
    if DEFAULT_UPLOAD_SPEED not in speeds:
        sys.exit(f"UploadSpeed に {DEFAULT_UPLOAD_SPEED} がありません")
    ordered_speeds = {DEFAULT_UPLOAD_SPEED: speeds[DEFAULT_UPLOAD_SPEED]}
    ordered_speeds.update(speeds)
    menus["UploadSpeed"] = ordered_speeds

    out = []
    out.append("# Pocorobo ESP32 Boards")
    out.append(
        f"# tools/make-boards-txt.py が arduino-esp32 {esp32_core_version()} の boards.txt"
        f"({BASE_BOARD}) から生成する。手で編集しない。"
    )
    for menu in KEEP_MENUS:
        out.append(f"menu.{menu}={menu_labels[menu]}")
    for board, overrides in BOARDS.items():
        out.append("")
        out.append("#" * 62)
        out.append("")
        values = dict(fixed)
        values.update(overrides)
        for key, value in values.items():
            out.append(f"{board}.{key}={value}")
        for menu in KEEP_MENUS:
            out.append("")
            for option, subs in menus[menu].items():
                for sub, value in subs:
                    key = f"menu.{menu}.{option}" + (f".{sub}" if sub else "")
                    out.append(f"{board}.{key}={value}")
    args.out.write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"{args.out}: {len(BOARDS)} boards from {source}")


if __name__ == "__main__":
    main()
