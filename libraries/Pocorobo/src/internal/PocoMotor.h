// DC モータ 1 台(正方向・負方向とも MCPWM で 50 Hz の PWM を出す)
#pragma once

#include <Arduino.h>

#include "driver/mcpwm_prelude.h"

class PocoMotor {
public:
  // 速度を設定(-100〜100 %、範囲外はクランプ。0 で停止)
  bool run(float percent);

  // 現在の速度(%)
  float percent() const { return m_percent; }

private:
  friend class PocoDevices;

  // モータ用の共通タイマ(MCPWM グループごとに 1 本)を作る。各モータの begin() より前に呼ぶ
  static bool newTimer(int group, mcpwm_timer_handle_t& timer);

  // 共通タイマを動かす。全モータの begin() の後に呼ぶ
  static bool startTimer(mcpwm_timer_handle_t timer);

  bool begin(uint8_t id, mcpwm_timer_handle_t timer, int group);

  // 正方向・負方向の添字
  static constexpr int kForward = 0;
  static constexpr int kReverse = 1;

  uint8_t             m_id      = 0xFF;  // 0xFF は未初期化(すべての操作が失敗する)
  float               m_percent = 0.0f;
  mcpwm_oper_handle_t m_oper    = nullptr;
  mcpwm_cmpr_handle_t m_cmpr[2] = {nullptr, nullptr};
  mcpwm_gen_handle_t  m_gen[2]  = {nullptr, nullptr};
};
