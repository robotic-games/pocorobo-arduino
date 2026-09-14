// RGB LED(1 個)
#pragma once

#include <Arduino.h>

class PocoLed {
public:
  // 色を設定(各 0〜100、範囲外はクランプ)
  void set(float r, float g, float b);

  // 消灯
  void off() { set(0.0f, 0.0f, 0.0f); }

  float r() const { return m_r; }
  float g() const { return m_g; }
  float b() const { return m_b; }

private:
  float m_r = 0.0f;
  float m_g = 0.0f;
  float m_b = 0.0f;
};
