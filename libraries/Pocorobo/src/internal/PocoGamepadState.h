// USB ゲームパッドの状態(W3C Gamepad API 準拠)
//
// https://www.w3.org/TR/gamepad/ の Gamepad / GamepadButton インターフェースに合わせた構造体。
// buttons[] は W3C Standard Gamepad Mapping の並び(添字は BTN_* 定数)、axes[] は -1.0〜1.0 の軸。
// 機種ごとの HID レポートからこの形へ変換するのは PocoGamepadDecoder の責務。
//
// ESP 依存の無いヘッダオンリー実装。
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

// W3C Gamepad インターフェース準拠(一部省略)
// https://www.w3.org/TR/gamepad/#gamepad-interface
struct PocoGamepadState {
  // W3C GamepadButton インターフェース準拠
  // https://www.w3.org/TR/gamepad/#gamepadbutton-interface
  struct GamepadButton {
    bool  pressed{false};  // ボタンが押されているか(デジタル状態)
    bool  touched{false};  // ボタンがタッチされているか(将来の拡張用)
    float value{0.0F};     // アナログ値(0.0〜1.0。W3C 標準は double だが float で十分)

    // 等価比較
    bool operator==(const GamepadButton&) const = default;
  };
  // W3C Standard Gamepad Mapping 準拠(拡張対応)
  // https://www.w3.org/TR/gamepad/#remapping
  static constexpr size_t kButtonCount = 17;
  static constexpr size_t kAxisCount   = 8;

  std::array<GamepadButton, kButtonCount> buttons{};  // ボタン状態
  std::array<float, kAxisCount>           axes{};     // アナログスティック + D-Pad 軸(-1.0〜1.0)
                                            // 標準: axes[0-3] = LX, LY, RX, RY
                                            // 非標準(JC-U3912TBK 等): axes[4-5] = D-Pad X/Y

  // ボタンインデックス定数(W3C Standard Gamepad Mapping 準拠)
  static constexpr int BTN_A     = 0;   // buttons[0]: 下ボタン (South)
  static constexpr int BTN_B     = 1;   // buttons[1]: 右ボタン (East)
  static constexpr int BTN_X     = 2;   // buttons[2]: 左ボタン (West)
  static constexpr int BTN_Y     = 3;   // buttons[3]: 上ボタン (North)
  static constexpr int BTN_LB    = 4;   // buttons[4]: 左肩ボタン
  static constexpr int BTN_RB    = 5;   // buttons[5]: 右肩ボタン
  static constexpr int BTN_LT    = 6;   // buttons[6]: 左トリガ(デジタル + アナログ)
  static constexpr int BTN_RT    = 7;   // buttons[7]: 右トリガ(デジタル + アナログ)
  static constexpr int BTN_BACK  = 8;   // buttons[8]: Back/Select
  static constexpr int BTN_START = 9;   // buttons[9]: Start
  static constexpr int BTN_L3    = 10;  // buttons[10]: 左スティック押し込み
  static constexpr int BTN_R3    = 11;  // buttons[11]: 右スティック押し込み
  static constexpr int BTN_UP    = 12;  // buttons[12]: 十字キー上
  static constexpr int BTN_DOWN  = 13;  // buttons[13]: 十字キー下
  static constexpr int BTN_LEFT  = 14;  // buttons[14]: 十字キー左
  static constexpr int BTN_RIGHT = 15;  // buttons[15]: 十字キー右
  static constexpr int BTN_HOME  = 16;  // buttons[16]: Home/Guide(W3C 拡張)

  // ボタン名マッピングテーブル
  static constexpr struct ButtonInfo {
    std::string_view name;
    int              index;
  } button_names[] = {
    {"A", BTN_A},
    {"B", BTN_B},
    {"X", BTN_X},
    {"Y", BTN_Y},
    {"LB", BTN_LB},
    {"RB", BTN_RB},
    {"LT", BTN_LT},
    {"RT", BTN_RT},
    {"START", BTN_START},
    {"BACK", BTN_BACK},
    {"L3", BTN_L3},
    {"R3", BTN_R3},
    {"UP", BTN_UP},
    {"DOWN", BTN_DOWN},
    {"LEFT", BTN_LEFT},
    {"RIGHT", BTN_RIGHT},
    {"HOME", BTN_HOME}};

  // 等価比較(テスト用)
  bool operator==(const PocoGamepadState&) const = default;
};
