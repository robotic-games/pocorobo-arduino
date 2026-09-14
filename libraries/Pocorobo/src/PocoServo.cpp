// サーボの実装(LEDC 50 Hz・13 bit)
#include "Pocorobo.h"

#include <algorithm>

namespace {
// サーボ PWM(LEDC)の設定: 50 Hz・13 bit
constexpr uint32_t kPwmFrequencyHz    = 50;
constexpr uint8_t  kPwmResolutionBits = 13;
constexpr uint32_t kPwmMaxValue       = 8191;      // 2^13 - 1
constexpr float    kPwmPeriodUs       = 20000.0f;  // 50 Hz = 20 ms

// パルス幅(µs)を 13 bit デューティに変換
uint32_t microsecondsToDuty(float pulseUs) {
  return static_cast<uint32_t>((pulseUs / kPwmPeriodUs) * kPwmMaxValue);
}
}  // namespace

bool PocoServo::begin(uint8_t id) {
  if (!ledcAttach(PIN_SERVO[id], kPwmFrequencyHz, kPwmResolutionBits)) {
    log_e("servo %u: ledcAttach failed (pin %u)", id, PIN_SERVO[id]);
    return false;
  }
  // 命令が来るまで無出力
  ledcWrite(PIN_SERVO[id], 0);
  m_id      = id;
  m_pulseUs = 0.0f;
  return true;
}

bool PocoServo::angle(float deg) {
  deg = std::clamp(deg, 0.0f, 180.0f);
  return pulse(500.0f + (deg / 180.0f) * 2000.0f);
}

bool PocoServo::pulse(float us) {
  if (m_id >= POCOROBO_SERVO_COUNT) {
    return false;
  }
  us        = std::clamp(us, 500.0f, 2500.0f);
  m_pulseUs = us;
  return ledcWrite(PIN_SERVO[m_id], microsecondsToDuty(us));
}

void PocoServo::stop() {
  if (m_id >= POCOROBO_SERVO_COUNT) {
    return;
  }
  m_pulseUs = 0.0f;
  ledcWrite(PIN_SERVO[m_id], 0);
}
