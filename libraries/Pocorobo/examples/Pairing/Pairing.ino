// 専用コントローラとペアリングする例
// 本体のボタンを 2 秒長押しするとペアリングを始める。コントローラ側のペアリングボタンも押すこと。
// 状態を LED に出す: 待受(ペアリング中) = 青点滅、接続 = 緑、失敗 = 赤、未接続 = 青、未ペアリング = 消灯
// 状態が変わるたびにシリアルモニタ(115200)にも出す。
// 一度ペアリングすれば本体に保存され、次回からは電源を入れるだけでつながる。
//
// 標準のプログラムが入っている本体は、初回だけ BOOT を押しながら RST を押して書き込みモードにしてから書き込む
#include <Pocorobo.h>

namespace {
constexpr uint32_t kLongPressMs = 2000;

const char* stateName(PocoController::State state) {
  switch (state) {
    case PocoController::State::Unpaired:
      return "未ペアリング";
    case PocoController::State::Pairing:
      return "ペアリング中(コントローラのペアリングボタンを押してください)";
    case PocoController::State::PairingFailed:
      return "ペアリング失敗";
    case PocoController::State::Disconnected:
      return "未接続";
    case PocoController::State::Connected:
      return "接続";
  }
  return "?";
}

PocoController::State lastState  = PocoController::State::Unpaired;
uint32_t              pressStart = 0;      // ボタンを押し始めた時刻(0 = 押していない)
bool                  pressUsed  = false;  // この押下でペアリングを始めた
}  // namespace

void setup() {
  Poco.begin();
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);  // 押すと LOW
  lastState = Poco.controller.state();
  Serial.println(stateName(lastState));
}

void loop() {
  const uint32_t now = millis();

  // 本体ボタンの長押しでペアリング開始(押し続けても 1 回だけ)
  if (digitalRead(PIN_BUTTON) == LOW) {
    if (pressStart == 0) {
      pressStart = now;
      pressUsed  = false;
    } else if (!pressUsed && now - pressStart >= kLongPressMs) {
      pressUsed = true;
      Poco.controller.startPairing();
      Serial.println("ペアリングを始めます");
    }
  } else {
    pressStart = 0;
  }

  // 状態の変化を表示
  const PocoController::State state = Poco.controller.state();
  if (state != lastState) {
    lastState = state;
    Serial.println(stateName(state));
  }

  // LED
  switch (state) {
    case PocoController::State::Pairing:
      if ((now / 250) % 2 == 0) {
        Poco.led.set(0, 0, 100);
      } else {
        Poco.led.off();
      }
      break;
    case PocoController::State::Connected:
      Poco.led.set(0, 100, 0);
      break;
    case PocoController::State::PairingFailed:
      Poco.led.set(100, 0, 0);
      break;
    case PocoController::State::Disconnected:
      Poco.led.set(0, 0, 100);
      break;
    case PocoController::State::Unpaired:
      Poco.led.off();
      break;
  }
  delay(20);
}
