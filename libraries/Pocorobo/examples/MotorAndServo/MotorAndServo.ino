// モータとサーボを動かす例
// モータ 0 を +50 % で 1 秒 → 停止 1 秒 → -50 % で 1 秒 → 停止 1 秒、
// そのあとサーボ 0 を 0 → 90 → 180 度に動かす、を繰り返す
//
// 標準のプログラム（スタジオ用）が入っている本体は、最初に Arduino 用へ切り替えてから書き込む（手順は README）
#include <Pocorobo.h>

void setup() {
  Poco.begin();
}

void loop() {
  Poco.motor(0).run(50);  // 正転 50 %
  delay(1000);
  Poco.motor(0).run(0);  // 停止
  delay(1000);
  Poco.motor(0).run(-50);  // 逆転 50 %
  delay(1000);
  Poco.motor(0).run(0);  // 停止
  delay(1000);

  Poco.servo(0).angle(0);
  delay(1000);
  Poco.servo(0).angle(90);
  delay(1000);
  Poco.servo(0).angle(180);
  delay(1000);
}
