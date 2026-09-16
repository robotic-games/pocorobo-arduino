// USB ゲームパッドの HID 入力レポートのデコード実装
//
// 機種ごとのバイト配置を PocoGamepadState(W3C Standard Gamepad Mapping)へ写す。
// 軸は中立 0.0・両端が厳密に ±1.0 になるよう正規化する(理由は normalizeAxis8 の注記)。
#include "internal/PocoGamepadDecoder.h"

namespace {
// 定数定義
constexpr int ELECOM_DATA_LENGTH  = 6;
constexpr int XBOX_DATA_LENGTH_20 = 20;
constexpr int XBOX_DATA_LENGTH_32 = 32;
constexpr int HORIPAD_DATA_LENGTH = 48;  // HORIPAD STEAM: Report ID 7 の入力レポート長

constexpr uint8_t HORIPAD_REPORT_ID = 0x07;

constexpr uint8_t TRIGGER_THRESHOLD = 30;
constexpr float   AXIS_CENTER       = 0x80;

// 軸の正規化。中立を 0.0、両端を厳密に ±1.0 にする。
//
// 生値の範囲は中立を挟んで正負非対称(8bit なら -128〜+127、16bit なら -32768〜+32767)
// のため、正負を同じ数で割ると正の端だけが 1.0 に届かない。正負それぞれの端の絶対値で
// 割ることで、どちらの端も厳密に ±1.0 になる。
//
// 端点が厳密であることは利用者から見える仕様である。スティックの値は `軸 * 100` で
// -100〜100 として公開されるため、端が 1 ULP でもずれると「スティックの値 ＝ 100」が
// 永久に成立しなくなる。

// 8bit HID 軸(0-255・中立 0x80)を -1.0〜1.0 へ
float normalizeAxis8(uint8_t raw) {
  const float centered = static_cast<float>(raw) - AXIS_CENTER;  // -128.0 〜 +127.0
  return centered >= 0.0F ? centered / 127.0F : centered / 128.0F;
}

// 16bit 符号付き軸(-32768〜32767)を -1.0〜1.0 へ
float normalizeAxis16(int16_t raw) {
  return raw >= 0 ? static_cast<float>(raw) / 32767.0F : static_cast<float>(raw) / 32768.0F;
}

// 16bit 符号付き軸を上下反転して -1.0〜1.0 へ。
// ゲームパッドは上を正とするが、公開する値は W3C 準拠で上を負とするため。
// 中立を明示的に返すのは、-0.0F を作らないため(値としては 0.0F と等しいが、
// 文字列化すると "-0.0" と表示されるのを避ける)。
float normalizeAxis16Flipped(int16_t raw) {
  if (raw == 0) {
    return 0.0F;
  }
  return -normalizeAxis16(raw);
}
}  // namespace

std::optional<PocoGamepadState> PocoGamepadDecoder::decode(const uint8_t* data, int length) {
  if (length == ELECOM_DATA_LENGTH) {
    return decodeElecom(data);
  }
  if (length == XBOX_DATA_LENGTH_20 || length == XBOX_DATA_LENGTH_32) {
    return decodeXbox(data);
  }
  if (length == HORIPAD_DATA_LENGTH && data[0] == HORIPAD_REPORT_ID) {
    return decodeHoripad(data);
  }
  // 上記以外(HORIPAD の別 Report ID レポート等)は意図的に無視し、直近状態を維持する
  return std::nullopt;
}

PocoGamepadState PocoGamepadDecoder::decodeElecom(const uint8_t* data) {
  PocoGamepadState gamepad{};

  // ボタンマッピング(JC-U3912TBK → Standard Gamepad 準拠)
  // HID ビット → 論理ボタン
  // 0:X, 1:Y, 2:A, 3:B, 4:LT, 5:RT, 6:LB, 7:RB, 8:L3, 9:R3, 10:Select, 11:Start
  const uint8_t buttons_raw[12] = {
    static_cast<uint8_t>((data[0] & 0x01) != 0),  // 0: X
    static_cast<uint8_t>((data[0] & 0x02) != 0),  // 1: Y
    static_cast<uint8_t>((data[0] & 0x04) != 0),  // 2: A
    static_cast<uint8_t>((data[0] & 0x08) != 0),  // 3: B
    static_cast<uint8_t>((data[0] & 0x10) != 0),  // 4: LT
    static_cast<uint8_t>((data[0] & 0x20) != 0),  // 5: RT
    static_cast<uint8_t>((data[0] & 0x40) != 0),  // 6: LB
    static_cast<uint8_t>((data[0] & 0x80) != 0),  // 7: RB
    static_cast<uint8_t>((data[1] & 0x01) != 0),  // 8: L3
    static_cast<uint8_t>((data[1] & 0x02) != 0),  // 9: R3
    static_cast<uint8_t>((data[1] & 0x04) != 0),  // 10: Select
    static_cast<uint8_t>((data[1] & 0x08) != 0),  // 11: Start
  };

  // Standard Gamepad Mapping に配置
  gamepad.buttons[PocoGamepadState::BTN_A].pressed     = (buttons_raw[2] != 0u);   // A
  gamepad.buttons[PocoGamepadState::BTN_B].pressed     = (buttons_raw[3] != 0u);   // B
  gamepad.buttons[PocoGamepadState::BTN_X].pressed     = (buttons_raw[0] != 0u);   // X
  gamepad.buttons[PocoGamepadState::BTN_Y].pressed     = (buttons_raw[1] != 0u);   // Y
  gamepad.buttons[PocoGamepadState::BTN_LB].pressed    = (buttons_raw[6] != 0u);   // LB
  gamepad.buttons[PocoGamepadState::BTN_RB].pressed    = (buttons_raw[7] != 0u);   // RB
  gamepad.buttons[PocoGamepadState::BTN_LT].pressed    = (buttons_raw[4] != 0u);   // LT
  gamepad.buttons[PocoGamepadState::BTN_RT].pressed    = (buttons_raw[5] != 0u);   // RT
  gamepad.buttons[PocoGamepadState::BTN_BACK].pressed  = (buttons_raw[10] != 0u);  // Select
  gamepad.buttons[PocoGamepadState::BTN_START].pressed = (buttons_raw[11] != 0u);  // Start
  gamepad.buttons[PocoGamepadState::BTN_L3].pressed    = (buttons_raw[8] != 0u);   // L3
  gamepad.buttons[PocoGamepadState::BTN_R3].pressed    = (buttons_raw[9] != 0u);   // R3

  // D-Pad(HID レポートのバイト 1 上位 4 ビット → buttons[12-15])
  const uint8_t dpad_value                             = (data[1] >> 4) & 0x0F;
  gamepad.buttons[PocoGamepadState::BTN_UP].pressed    = (dpad_value == 0 || dpad_value == 1 || dpad_value == 7);
  gamepad.buttons[PocoGamepadState::BTN_DOWN].pressed  = (dpad_value == 3 || dpad_value == 4 || dpad_value == 5);
  gamepad.buttons[PocoGamepadState::BTN_LEFT].pressed  = (dpad_value == 5 || dpad_value == 6 || dpad_value == 7);
  gamepad.buttons[PocoGamepadState::BTN_RIGHT].pressed = (dpad_value == 1 || dpad_value == 2 || dpad_value == 3);

  // 全ボタンの value/touched を一括設定
  for (auto& btn : gamepad.buttons) {
    btn.value   = btn.pressed ? 1.0F : 0.0F;
    btn.touched = btn.pressed;
  }

  // 軸マッピング(Standard Gamepad 準拠 - 4 軸のみ)
  gamepad.axes[0] = normalizeAxis8(data[2]);  // 左スティック X
  gamepad.axes[1] = normalizeAxis8(data[3]);  // 左スティック Y
  gamepad.axes[2] = normalizeAxis8(data[5]);  // 右スティック X (Rz 軸)
  gamepad.axes[3] = normalizeAxis8(data[4]);  // 右スティック Y (Z 軸)

  return gamepad;
}

PocoGamepadState PocoGamepadDecoder::decodeXbox(const uint8_t* data) {
  PocoGamepadState gamepad{};

  const auto buttons1 = data[2];
  const auto buttons2 = data[3];
  const auto lt       = data[4];
  const auto rt       = data[5];

  const auto lx = static_cast<int16_t>((data[7] << 8) | data[6]);
  const auto ly = static_cast<int16_t>((data[9] << 8) | data[8]);
  const auto rx = static_cast<int16_t>((data[11] << 8) | data[10]);
  const auto ry = static_cast<int16_t>((data[13] << 8) | data[12]);

  // ボタンマッピング(Xbox 360 公式仕様準拠)
  // Byte 2 (buttons1): bits 0-3=D-pad, bit4=Start, bit5=Back, bit6=L3, bit7=R3
  gamepad.buttons[PocoGamepadState::BTN_UP].pressed    = ((buttons1 & 0x01) != 0);
  gamepad.buttons[PocoGamepadState::BTN_DOWN].pressed  = ((buttons1 & 0x02) != 0);
  gamepad.buttons[PocoGamepadState::BTN_LEFT].pressed  = ((buttons1 & 0x04) != 0);
  gamepad.buttons[PocoGamepadState::BTN_RIGHT].pressed = ((buttons1 & 0x08) != 0);
  gamepad.buttons[PocoGamepadState::BTN_START].pressed = ((buttons1 & 0x10) != 0);
  gamepad.buttons[PocoGamepadState::BTN_BACK].pressed  = ((buttons1 & 0x20) != 0);
  gamepad.buttons[PocoGamepadState::BTN_L3].pressed    = ((buttons1 & 0x40) != 0);
  gamepad.buttons[PocoGamepadState::BTN_R3].pressed    = ((buttons1 & 0x80) != 0);

  // Byte 3 (buttons2): bit0=LB, bit1=RB, bit2=Xbox, bit4=A, bit5=B, bit6=X, bit7=Y
  gamepad.buttons[PocoGamepadState::BTN_LB].pressed   = ((buttons2 & 0x01) != 0);
  gamepad.buttons[PocoGamepadState::BTN_RB].pressed   = ((buttons2 & 0x02) != 0);
  gamepad.buttons[PocoGamepadState::BTN_HOME].pressed = ((buttons2 & 0x04) != 0);
  gamepad.buttons[PocoGamepadState::BTN_A].pressed    = ((buttons2 & 0x10) != 0);
  gamepad.buttons[PocoGamepadState::BTN_B].pressed    = ((buttons2 & 0x20) != 0);
  gamepad.buttons[PocoGamepadState::BTN_X].pressed    = ((buttons2 & 0x40) != 0);
  gamepad.buttons[PocoGamepadState::BTN_Y].pressed    = ((buttons2 & 0x80) != 0);

  gamepad.buttons[PocoGamepadState::BTN_LT].value   = lt / 255.0F;
  gamepad.buttons[PocoGamepadState::BTN_LT].pressed = (lt > TRIGGER_THRESHOLD);
  gamepad.buttons[PocoGamepadState::BTN_RT].value   = rt / 255.0F;
  gamepad.buttons[PocoGamepadState::BTN_RT].pressed = (rt > TRIGGER_THRESHOLD);

  for (int i = 0; i < 17; i++) {
    if (i != PocoGamepadState::BTN_LT && i != PocoGamepadState::BTN_RT) {
      gamepad.buttons[i].value = gamepad.buttons[i].pressed ? 1.0F : 0.0F;
    }
  }

  gamepad.axes[0] = normalizeAxis16(lx);
  gamepad.axes[1] = normalizeAxis16Flipped(ly);
  gamepad.axes[2] = normalizeAxis16(rx);
  gamepad.axes[3] = normalizeAxis16Flipped(ry);

  return gamepad;
}

// HORIPAD STEAM(VID 0x0F0D / PID 0x01AB)の HID レポートをデコード。
// レポート構造(48 バイト固定・先頭に Report ID 0x07):
//   [1-4]=左X,左Y,右X,右Y(0-255,中央0x80) / [5]下位4bit=Hat,上位4bit=HIDボタン1-4
//   [6]=HIDボタン5-12 / [7]=HIDボタン13-20 / [8]=RTアナログ / [9]=LTアナログ / [10-]=ベンダ(無視)
// ボタン対応は SDL 公式 DB(0f0d:01ab)+ 実機捕捉で確認済み(HID ボタン n = bit(n-1))。
PocoGamepadState PocoGamepadDecoder::decodeHoripad(const uint8_t* data) {
  PocoGamepadState gamepad{};

  // 20 個の HID ボタンを 1 つのビット列に展開(HID ボタン n = bit(n-1))
  const uint32_t buttons = (static_cast<uint32_t>(data[5]) >> 4) |
                           (static_cast<uint32_t>(data[6]) << 4) |
                           (static_cast<uint32_t>(data[7]) << 12);
  const auto pressed = [buttons](int hidButton) {
    return (buttons & (1U << (hidButton - 1))) != 0U;
  };

  // ボタンマッピング(HID ボタン番号 → Standard Gamepad)
  gamepad.buttons[PocoGamepadState::BTN_A].pressed     = pressed(1);
  gamepad.buttons[PocoGamepadState::BTN_B].pressed     = pressed(2);
  gamepad.buttons[PocoGamepadState::BTN_X].pressed     = pressed(4);
  gamepad.buttons[PocoGamepadState::BTN_Y].pressed     = pressed(5);
  gamepad.buttons[PocoGamepadState::BTN_LB].pressed    = pressed(7);
  gamepad.buttons[PocoGamepadState::BTN_RB].pressed    = pressed(8);
  gamepad.buttons[PocoGamepadState::BTN_BACK].pressed  = pressed(11);  // View
  gamepad.buttons[PocoGamepadState::BTN_START].pressed = pressed(12);  // Menu
  gamepad.buttons[PocoGamepadState::BTN_HOME].pressed  = pressed(13);  // STEAM
  gamepad.buttons[PocoGamepadState::BTN_L3].pressed    = pressed(14);
  gamepad.buttons[PocoGamepadState::BTN_R3].pressed    = pressed(15);

  // トリガ: アナログ値([8]=RT, [9]=LT)+ デジタルボタン(btn10/btn9)の OR で押下判定
  const uint8_t rt                                  = data[8];
  const uint8_t lt                                  = data[9];
  gamepad.buttons[PocoGamepadState::BTN_RT].value   = rt / 255.0F;
  gamepad.buttons[PocoGamepadState::BTN_RT].pressed = (rt > TRIGGER_THRESHOLD) || pressed(10);
  gamepad.buttons[PocoGamepadState::BTN_LT].value   = lt / 255.0F;
  gamepad.buttons[PocoGamepadState::BTN_LT].pressed = (lt > TRIGGER_THRESHOLD) || pressed(9);

  // D-Pad(Hat: [5]下位4bit。0/2/4/6=上右下左、斜め 1/3/5/7、中立 0xF/8)
  const uint8_t hat                                    = data[5] & 0x0F;
  gamepad.buttons[PocoGamepadState::BTN_UP].pressed    = (hat == 0 || hat == 1 || hat == 7);
  gamepad.buttons[PocoGamepadState::BTN_DOWN].pressed  = (hat == 3 || hat == 4 || hat == 5);
  gamepad.buttons[PocoGamepadState::BTN_LEFT].pressed  = (hat == 5 || hat == 6 || hat == 7);
  gamepad.buttons[PocoGamepadState::BTN_RIGHT].pressed = (hat == 1 || hat == 2 || hat == 3);

  // 非トリガボタンの value を設定し、全ボタンの touched を pressed に揃える
  for (int i = 0; i < 17; i++) {
    if (i != PocoGamepadState::BTN_LT && i != PocoGamepadState::BTN_RT) {
      gamepad.buttons[i].value = gamepad.buttons[i].pressed ? 1.0F : 0.0F;
    }
    gamepad.buttons[i].touched = gamepad.buttons[i].pressed;
  }

  // 軸(0-255,中央0x80 → -1.0〜1.0。HID 準拠で Y 反転なし)
  gamepad.axes[0] = normalizeAxis8(data[1]);  // 左スティック X
  gamepad.axes[1] = normalizeAxis8(data[2]);  // 左スティック Y
  gamepad.axes[2] = normalizeAxis8(data[3]);  // 右スティック X
  gamepad.axes[3] = normalizeAxis8(data[4]);  // 右スティック Y

  return gamepad;
}
