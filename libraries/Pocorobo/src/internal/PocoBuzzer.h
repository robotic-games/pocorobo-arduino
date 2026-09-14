// ブザー(MCPWM で周波数可変の 50 % デューティ矩形波)
#pragma once

#include <Arduino.h>

#include "driver/mcpwm_prelude.h"

class PocoBuzzer {
public:
  // 鳴らす(100〜10000 Hz、範囲外はクランプ。0 以下で停止)
  bool tone(float hz);

  // 止める(出力を LOW に固定)
  bool off();

  // 現在の周波数(Hz)。停止中は 0
  float frequency() const { return m_hz; }

private:
  friend class PocoDevices;
  bool begin();

  float                m_hz    = 0.0f;
  mcpwm_timer_handle_t m_timer = nullptr;
  mcpwm_oper_handle_t  m_oper  = nullptr;
  mcpwm_cmpr_handle_t  m_cmpr  = nullptr;
  mcpwm_gen_handle_t   m_gen   = nullptr;
};
