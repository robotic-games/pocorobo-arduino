// 競技システム ATTRACTS の操縦者入力でロボットを動かす例(Standard のみ)
// トランシーバの信号線を UART コネクタの RX につないでおく。
// W / S で前後、A / D で旋回(モータ 0 = 左、1 = 右と仮定)。マウスの左右の動きでサーボ 0 を振る。
// マウス左ボタンを押している間サーボ 1 を 60 度、離すと 120 度にする。
// 入力が届いていない間はモータを止める。LED: 接続 = チームの色(赤 / 青)、未接続 = 消灯。
// 機体の HP とヒートを 0.5 秒ごとにシリアルモニタ(115200)に出す。
// (UART コネクタの無い機種では、その旨を出すだけ)
//
// 標準のプログラムが入っている本体は、初回だけ BOOT を押しながら RST を押して書き込みモードにしてから書き込む
#include <Pocorobo.h>

#if defined(POCOROBO_BOARD_STANDARD)

using Key = PocoAttracts::Key;

int           aim         = 90;  // サーボ 0 の向き(度)
unsigned long lastPrintMs = 0;

void setup() {
  Serial.begin(115200);
  Poco.begin();
  Poco.attracts.begin();
}

void loop() {
  if (!Poco.attracts.isConnected()) {
    Poco.motor(0).run(0);
    Poco.motor(1).run(0);
    Poco.led.off();
    delay(10);
    return;
  }
  Poco.led.set(Poco.attracts.team() == 0 ? 100 : 0, 0, Poco.attracts.team() == 1 ? 100 : 0);

  // キーは押されている間 true。前後と旋回を足し合わせる
  int forward = 0;
  int turn    = 0;
  if (Poco.attracts.key(Key::W)) forward += 60;
  if (Poco.attracts.key(Key::S)) forward -= 60;
  if (Poco.attracts.key(Key::D)) turn += 40;
  if (Poco.attracts.key(Key::A)) turn -= 40;
  Poco.motor(0).run(forward + turn);  // 左(100 % を超える分はクランプされる)
  Poco.motor(1).run(forward - turn);  // 右

  // マウスの左右の移動量(前回読んでからの差分)でサーボ 0 の向きを変える
  aim = constrain(aim + Poco.attracts.mouseDeltaX() / 4, 0, 180);
  Poco.servo(0).angle(aim);

  Poco.servo(1).angle(Poco.attracts.key(Key::MouseLeft) ? 60 : 120);

  if (millis() - lastPrintMs >= 500) {
    lastPrintMs = millis();
    Serial.printf("HP %d/%d  heat %d/%d\n", Poco.attracts.hp(), Poco.attracts.maxHp(), Poco.attracts.heat(), Poco.attracts.maxHeat());
  }
  delay(10);
}

#else

void setup() {
  Poco.begin();
  Serial.begin(115200);
  // シリアルモニタが開くのを少し待ってから 1 回だけ出す
  const uint32_t start = millis();
  while (!Serial && millis() - start < 3000) {
    delay(10);
  }
  Serial.println("この機種には UART コネクタが無いので ATTRACTS をつなげません");
}

void loop() {
}

#endif
