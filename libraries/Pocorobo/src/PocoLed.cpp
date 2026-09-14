// RGB LED の実装
#include "Pocorobo.h"

#include <algorithm>

void PocoLed::set(float r, float g, float b) {
  m_r = std::clamp(r, 0.0f, 100.0f);
  m_g = std::clamp(g, 0.0f, 100.0f);
  m_b = std::clamp(b, 0.0f, 100.0f);

  // 0〜100 を 0〜255 に換算
  rgbLedWrite(PIN_LED,
    static_cast<uint8_t>(m_r * 2.55f),
    static_cast<uint8_t>(m_g * 2.55f),
    static_cast<uint8_t>(m_b * 2.55f));
}
