// USB ゲームパッドの HID 入力レポートを W3C Gamepad 形式(PocoGamepadState)へ変換するデコーダ
//
// 対応機種はレポート長(と先頭の Report ID)で見分ける:
//   6 バイト        エレコム JC-U3912TBK
//   20 / 32 バイト  XInput(Xbox 360 互換。8BitDo Ultimate 2C / Flydigi DUNE FOX のドングル等)
//   48 バイト       HORIPAD STEAM(Report ID 0x07 の入力レポートのみ)
// それ以外のレポートは nullopt を返し、呼び出し側は直近の状態を維持する。
//
// ESP 依存の無い実装。ログ・副作用を持たない。
#pragma once

#include <cstdint>
#include <optional>

#include "PocoGamepadState.h"

// HID デコーダ(静的クラス)
class PocoGamepadDecoder {
public:
  // HID データを W3C Gamepad 形式にデコード
  [[nodiscard]] static std::optional<PocoGamepadState> decode(const uint8_t* data, int length);

private:
  // デコード実装
  static PocoGamepadState decodeElecom(const uint8_t* data);
  static PocoGamepadState decodeXbox(const uint8_t* data);
  static PocoGamepadState decodeHoripad(const uint8_t* data);
};
