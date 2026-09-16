// USB ゲームパッド(Type-A コネクタ)でロボットを動かす例
// 左スティックの上下で前後、左右で旋回(モータ 0 = 左、1 = 右と仮定)。
// A ボタン(0)を押している間サーボ 0 を 60 度、離すと 120 度にする。
// USB ゲームパッドが無いときは専用コントローラ(無線)の十字キーで同じ操作をする。
// LED: USB ゲームパッド = 緑、専用コントローラ = 水色、どちらも無し = 青
//
// USB の切り替えはライブラリが自動で行う:
//   パソコンをつないでいる間は Type-C(書き込みとシリアルモニタ)
//   パソコンを外して電池で動かすと Type-A に切り替わり、ゲームパッドを待つ
// ゲームパッドで動かしている本体に書き込むときは、パソコンをつないでポートが出るまで(数秒)待ってから書く。
// USB ゲームパッドがつながっている間、専用コントローラの無線は止まる(ペアリングもできない)。
//
// 標準のプログラムが入っている本体は、初回だけ BOOT を押しながら RST を押して書き込みモードにしてから書き込む
#include <Pocorobo.h>

void setup() {
  Poco.begin();
  Poco.gamepad.begin();
}

void loop() {
  int  x       = 0;
  int  forward = 0;
  bool buttonA = false;

  if (Poco.gamepad.isConnected()) {
    Poco.led.set(0, 100, 0);
    x       = Poco.gamepad.axis(0);   // 左スティック左右(右が正)
    forward = -Poco.gamepad.axis(1);  // 上下は下が正なので、反転して前が正にする
    buttonA = Poco.gamepad.button(0);
  } else if (Poco.controller.isConnected()) {
    Poco.led.set(0, 100, 100);
    x       = Poco.controller.axis(0);
    forward = -Poco.controller.axis(1);
    buttonA = Poco.controller.button(0);
  } else {
    Poco.led.set(0, 0, 100);
    Poco.motor(0).run(0);
    Poco.motor(1).run(0);
    delay(10);
    return;
  }

  Poco.motor(0).run(forward + x);  // 左(100 % を超える分はクランプされる)
  Poco.motor(1).run(forward - x);  // 右
  Poco.servo(0).angle(buttonA ? 60 : 120);
  delay(10);
}
