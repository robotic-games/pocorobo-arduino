// ポコロボの装置制御ライブラリ
// サーボ・DC モータ・RGB LED・ブザー・エンコーダをまとめて扱う。これ 1 つを include すれば使える。
//
// 使い方:
//   Poco.begin();                    // setup() の最初に呼ぶ
//   Poco.servo(0).angle(90);         // 角度 0〜180 度(90 度 = 1500 µs)
//   Poco.servo(0).pulse(1500);       // パルス幅 500〜2500 µs
//   Poco.motor(0).run(50);           // 速度 -100〜100 %(負で逆転)
//   Poco.led.set(100, 0, 0);         // 各 0〜100
//   Poco.buzzer.tone(440);           // 100〜10000 Hz(0 以下で停止)。ブザーの無い機種では使えない
//   Poco.buzzer.off();
//   Poco.encoder(0).count();         // 累積カウント(正転で増加・逆転で減少)。エンコーダの無い機種では使えない
//   Poco.encoder(0).reset();
//   Poco.stop();                     // 全装置を初期状態へ
//
// 機種ごとの装置の有無は POCOROBO_HAS_BUZZER / POCOROBO_HAS_ENCODER、個数は POCOROBO_*_COUNT で分かる
#pragma once

#include <Arduino.h>

#if !defined(POCOROBO_BOARD_STANDARD) && !defined(POCOROBO_BOARD_MINI)
#error "ボードに Pocorobo Standard または Pocorobo Mini を選んでください"
#endif

#include "internal/PocoLed.h"
#include "internal/PocoMotor.h"
#include "internal/PocoServo.h"
#if POCOROBO_HAS_BUZZER
#include "internal/PocoBuzzer.h"
#endif
#if POCOROBO_HAS_ENCODER
#include "internal/PocoEncoder.h"
#endif

// 全装置のまとめ。グローバル変数 Poco を使う
class PocoDevices {
public:
  // 全装置を初期化する。USB 切替をパソコン側に固定してから各装置を初期化する
  // 1 つでも失敗したら false(失敗した装置の操作は無効になるが、他は使える)
  bool begin();

  // id が範囲外のときは操作が無効なダミーを返す
  PocoServo& servo(uint8_t id);
  PocoMotor& motor(uint8_t id);
#if POCOROBO_HAS_ENCODER
  PocoEncoder& encoder(uint8_t id);
#endif

  PocoLed led;
#if POCOROBO_HAS_BUZZER
  PocoBuzzer buzzer;
#endif

  // 全装置を初期状態へ(サーボ: PWM 停止、モータ: 0、LED: 消灯、ブザー: 停止、エンコーダ: 0)
  void stop();

private:
  bool beginServos();
  bool beginMotors();
#if POCOROBO_HAS_ENCODER
  bool beginEncoders();
#endif

  // モータ用 MCPWM タイマ(グループごとに 1 本)。1 グループにオペレータは 3 個までなので、4 台目からグループ 1
  static constexpr int kMotorMcpwmGroups = (POCOROBO_MOTOR_COUNT > 3) ? 2 : 1;

  PocoServo            m_servos[POCOROBO_SERVO_COUNT];
  PocoMotor            m_motors[POCOROBO_MOTOR_COUNT];
#if POCOROBO_HAS_ENCODER
  PocoEncoder          m_encoders[POCOROBO_ENCODER_COUNT];
#endif
  mcpwm_timer_handle_t m_motorTimer[kMotorMcpwmGroups] = {};
  bool                 m_began                         = false;
};

extern PocoDevices Poco;
