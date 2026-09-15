// エンコーダを読む例
// モータ 0 を +30 % で回しながら、エンコーダ 0 の値を 100 ms ごとにシリアルモニタ(115200)に出す
// (エンコーダの無い機種では、その旨を出すだけ)
//
// 標準のプログラム（スタジオ用）が入っている本体は、最初に Arduino 用へ切り替えてから書き込む（手順は README）
#include <Pocorobo.h>

#if POCOROBO_HAS_ENCODER

void setup() {
  Poco.begin();
  Serial.begin(115200);
  Poco.motor(0).run(30);
}

void loop() {
  Serial.println(Poco.encoder(0).count());
  delay(100);
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
  Serial.println("この機種にはエンコーダがありません");
}

void loop() {
}

#endif
