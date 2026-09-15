// LED とブザーの例
// LED を赤 → 緑 → 青と光らせ、ブザーでドレミを鳴らす、を繰り返す(ブザーの無い機種では LED だけ)
//
// 標準のプログラム（スタジオ用）が入っている本体は、最初に Arduino 用へ切り替えてから書き込む（手順は README）
#include <Pocorobo.h>

void setup() {
  Poco.begin();
}

void loop() {
  Poco.led.set(100, 0, 0);  // 赤
  delay(500);
  Poco.led.set(0, 100, 0);  // 緑
  delay(500);
  Poco.led.set(0, 0, 100);  // 青
  delay(500);
  Poco.led.off();

#if POCOROBO_HAS_BUZZER
  Poco.buzzer.tone(262);  // ド
  delay(300);
  Poco.buzzer.tone(294);  // レ
  delay(300);
  Poco.buzzer.tone(330);  // ミ
  delay(300);
  Poco.buzzer.off();
#endif

  delay(500);
}
