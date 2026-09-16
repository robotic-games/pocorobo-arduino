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
//   Poco.stop();                     // 全装置を初期状態へ(無線には触れない)
//   Poco.controller.isConnected();   // 専用コントローラから入力が届いていれば true
//   Poco.controller.axis(1);         // 十字キー(0 = 左右・1 = 上下、-100〜100)。button(0) でボタン
//   Poco.controller.startPairing();  // 初回だけ。コントローラのペアリングボタンも押す(以後は自動でつながる)
//   Poco.attracts.begin();           // 競技システム ATTRACTS のトランシーバを UART コネクタにつないだとき(Standard のみ)
//   Poco.attracts.key(PocoAttracts::Key::W);  // 操縦者のキー。mouseDeltaX() でマウス、hp() などで機体の状態
//   Poco.gamepad.begin();            // USB ゲームパッド(Type-A)を使うとき。パソコンが無いときだけ Type-A に切り替わる
//   Poco.gamepad.axis(0);            // スティック(-100〜100)。button(0) でボタン。isConnected() でつながっているか
//
// 機種ごとの装置の有無は POCOROBO_HAS_BUZZER / POCOROBO_HAS_ENCODER、個数は POCOROBO_*_COUNT で分かる
#pragma once

#include <Arduino.h>

#if !defined(POCOROBO_BOARD_STANDARD) && !defined(POCOROBO_BOARD_MINI)
#error "ボードに Pocorobo Standard または Pocorobo Mini を選んでください"
#endif

#include "internal/PocoController.h"
#include "internal/PocoGamepad.h"
#include "internal/PocoLed.h"
#include "internal/PocoMotor.h"
#include "internal/PocoServo.h"
#if POCOROBO_HAS_BUZZER
#include "internal/PocoBuzzer.h"
#endif
#if POCOROBO_HAS_ENCODER
#include "internal/PocoEncoder.h"
#endif
#if defined(POCOROBO_BOARD_STANDARD)
#include "internal/PocoAttracts.h"
#endif

// 全装置のまとめ。グローバル変数 Poco を使う
class PocoDevices {
public:
  // 全装置を初期化する。USB 切替をパソコン側に固定してから各装置を初期化し、最後に専用コントローラの受信を始める
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
  PocoController controller;  // 専用コントローラ(無線)。スケッチ側で WiFi ライブラリを使うと干渉する
  PocoGamepad    gamepad;     // USB ゲームパッド(Type-A)。使うときは gamepad.begin() を呼ぶ。つながっている間は無線が止まる
#if defined(POCOROBO_BOARD_STANDARD)
  PocoAttracts attracts;  // 競技システム ATTRACTS(UART コネクタ)。使うときは attracts.begin() を呼ぶ
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
