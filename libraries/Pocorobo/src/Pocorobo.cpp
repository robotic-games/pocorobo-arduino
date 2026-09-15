// 全装置のまとめ(PocoDevices)の実装
#include "Pocorobo.h"

namespace {
// モータ i が使う MCPWM グループ。1 グループにオペレータは 3 個までなので、4 台目からグループ 1
constexpr int motorGroup(int i) {
  return (i < 3) ? 0 : 1;
}

// 操作が無効なダミー(id 範囲外のときに返す)
PocoServo s_invalidServo;
PocoMotor s_invalidMotor;
#if POCOROBO_HAS_ENCODER
PocoEncoder s_invalidEncoder;
#endif
}  // namespace

PocoDevices Poco;

bool PocoDevices::begin() {
  if (m_began) {
    return true;
  }
  m_began = true;

  // USB 切替をパソコン側に固定
  pinMode(PIN_USB_SELECT, OUTPUT);
  digitalWrite(PIN_USB_SELECT, LOW);

  bool ok = true;
  ok      = beginServos() && ok;
  ok      = beginMotors() && ok;
  led.off();  // 初回呼び出しで RMT が初期化され、消灯する
#if POCOROBO_HAS_BUZZER
  ok = buzzer.begin() && ok;
#endif
#if POCOROBO_HAS_ENCODER
  ok = beginEncoders() && ok;
#endif
  ok = controller.begin() && ok;  // 装置の初期化が全部済んでから
  return ok;
}

bool PocoDevices::beginServos() {
  bool ok = true;
  for (uint8_t i = 0; i < POCOROBO_SERVO_COUNT; i++) {
    ok = m_servos[i].begin(i) && ok;
  }
  return ok;
}

bool PocoDevices::beginMotors() {
  // 共通タイマ(グループごとに 1 本)を作ってから各モータをつなぎ、最後に動かす
  for (int grp = 0; grp < kMotorMcpwmGroups; grp++) {
    if (!PocoMotor::newTimer(grp, m_motorTimer[grp])) {
      return false;
    }
  }

  bool ok = true;
  for (uint8_t i = 0; i < POCOROBO_MOTOR_COUNT; i++) {
    const int grp = motorGroup(i);
    ok            = m_motors[i].begin(i, m_motorTimer[grp], grp) && ok;
  }

  for (int grp = 0; grp < kMotorMcpwmGroups; grp++) {
    if (!PocoMotor::startTimer(m_motorTimer[grp])) {
      return false;
    }
  }
  return ok;
}

#if POCOROBO_HAS_ENCODER
bool PocoDevices::beginEncoders() {
  bool ok = true;
  for (uint8_t i = 0; i < POCOROBO_ENCODER_COUNT; i++) {
    ok = m_encoders[i].begin(i) && ok;
  }
  return ok;
}
#endif

PocoServo& PocoDevices::servo(uint8_t id) {
  return (id < POCOROBO_SERVO_COUNT) ? m_servos[id] : s_invalidServo;
}

PocoMotor& PocoDevices::motor(uint8_t id) {
  return (id < POCOROBO_MOTOR_COUNT) ? m_motors[id] : s_invalidMotor;
}

#if POCOROBO_HAS_ENCODER
PocoEncoder& PocoDevices::encoder(uint8_t id) {
  return (id < POCOROBO_ENCODER_COUNT) ? m_encoders[id] : s_invalidEncoder;
}
#endif

void PocoDevices::stop() {
  for (auto& servo : m_servos) {
    servo.stop();
  }
  for (auto& motor : m_motors) {
    (void)motor.run(0.0f);
  }
  led.off();
#if POCOROBO_HAS_BUZZER
  (void)buzzer.off();
#endif
#if POCOROBO_HAS_ENCODER
  for (auto& encoder : m_encoders) {
    encoder.reset();
  }
#endif
}
