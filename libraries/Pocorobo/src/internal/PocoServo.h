// サーボ 1 本(LEDC 50 Hz・13 bit)
#pragma once

#include <Arduino.h>

class PocoServo {
public:
  // 角度を設定(0〜180 度、範囲外はクランプ。0 度 = 500 µs、90 度 = 1500 µs、180 度 = 2500 µs)
  bool angle(float deg);

  // パルス幅を設定(500〜2500 µs、範囲外はクランプ)
  bool pulse(float us);

  // PWM 出力を止める(デューティ 0 でピンを LOW に固定)
  void stop();

  // 現在のパルス幅(µs)。停止中は 0
  float pulseUs() const { return m_pulseUs; }

private:
  friend class PocoDevices;
  bool begin(uint8_t id);

  uint8_t m_id      = 0xFF;  // 0xFF は未初期化(すべての操作が失敗する)
  float   m_pulseUs = 0.0f;
};
