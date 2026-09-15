// 専用コントローラでロボットを動かす例(ペアリング済みが前提。ペアリングは Pairing の例を参照)
// 十字キーの上下で前後、左右で旋回(モータ 0 = 左、1 = 右と仮定)。
// A ボタン(0)を押している間サーボ 0 を 60 度、離すと 120 度にする。
// コントローラから入力が届いていない間はモータを止める。LED: 接続 = 緑、未接続 = 青
//
// 標準のプログラム（スタジオ用）が入っている本体は、最初に Arduino 用へ切り替えてから書き込む（手順は README）
#include <Pocorobo.h>

void setup() {
  Poco.begin();
}

void loop() {
  if (!Poco.controller.isConnected()) {
    Poco.motor(0).run(0);
    Poco.motor(1).run(0);
    Poco.led.set(0, 0, 100);
    delay(10);
    return;
  }
  Poco.led.set(0, 100, 0);

  const int x       = Poco.controller.axis(0);   // 左右(右が正)
  const int forward = -Poco.controller.axis(1);  // 上下は下が正なので、反転して前が正にする
  Poco.motor(0).run(forward + x);                // 左(100 % を超える分はクランプされる)
  Poco.motor(1).run(forward - x);                // 右

  Poco.servo(0).angle(Poco.controller.button(0) ? 60 : 120);
  delay(10);
}
