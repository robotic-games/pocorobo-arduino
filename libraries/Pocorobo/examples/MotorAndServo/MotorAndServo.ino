// モータとサーボを動かす例
// モータ 0 を +50 % で 1 秒 → 停止 1 秒 → -50 % で 1 秒 → 停止 1 秒、
// そのあとサーボ 0 を 0 → 90 → 180 度に動かす、を繰り返す
//
// 標準のプログラムが入っている本体は、初回だけ BOOT を押しながら RST を押して書き込みモードにしてから書き込む
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
