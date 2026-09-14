// DC モータの実装(正方向・負方向とも MCPWM)
//
// 1 台につき MCPWM のオペレータ 1 個を使い、その 2 組の比較器・生成器を正方向ピン・負方向ピンに割り当てる。
// タイマはグループごとに 1 本を全モータで共有する(1 MHz / 20000 tick = 50 Hz)。
// 動かさない側の生成器は LOW に固定し、動かす側だけ固定を解いて PWM を出す。
#include "Pocorobo.h"

#include <algorithm>
#include <cmath>

#include "internal/PocoCheck.h"

namespace {
// 共通タイマの設定: 1 MHz / 20000 ticks = 50 Hz(タイマは 16 bit なので 1 MHz に抑える)
constexpr uint32_t kMcpwmResolutionHz = 1000000;
constexpr uint32_t kMcpwmPeriodTicks  = 20000;
}  // namespace

bool PocoMotor::newTimer(int group, mcpwm_timer_handle_t& timer) {
  mcpwm_timer_config_t timerConfig = {
    .group_id      = group,
    .clk_src       = MCPWM_TIMER_CLK_SRC_DEFAULT,
    .resolution_hz = kMcpwmResolutionHz,
    .count_mode    = MCPWM_TIMER_COUNT_MODE_UP,
    .period_ticks  = kMcpwmPeriodTicks,
    .intr_priority = 0,
    .flags         = {}};
  POCO_CHECK(mcpwm_new_timer(&timerConfig, &timer), "motor timer");
  return true;
}

bool PocoMotor::startTimer(mcpwm_timer_handle_t timer) {
  POCO_CHECK(mcpwm_timer_enable(timer), "motor timer enable");
  POCO_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP), "motor timer start");
  return true;
}

bool PocoMotor::begin(uint8_t id, mcpwm_timer_handle_t timer, int group) {
  if (timer == nullptr) {
    return false;
  }

  // オペレータ 1 個を共通タイマにつなぐ
  mcpwm_operator_config_t operConfig = {
    .group_id      = group,
    .intr_priority = 0,
    .flags         = {}};
  POCO_CHECK(mcpwm_new_operator(&operConfig, &m_oper), "motor operator");
  POCO_CHECK(mcpwm_operator_connect_timer(m_oper, timer), "motor operator connect");

  // 正方向・負方向それぞれに比較器と生成器を 1 組ずつ
  const uint8_t pins[2] = {PIN_MOTOR_FWD[id], PIN_MOTOR_REV[id]};
  for (int dir = 0; dir < 2; dir++) {
    mcpwm_comparator_config_t cmprConfig = {
      .intr_priority = 0,
      .flags         = {}};
    POCO_CHECK(mcpwm_new_comparator(m_oper, &cmprConfig, &m_cmpr[dir]), "motor comparator");

    mcpwm_generator_config_t genConfig = {
      .gen_gpio_num = pins[dir],
      .flags        = {}};
    POCO_CHECK(mcpwm_new_generator(m_oper, &genConfig, &m_gen[dir]), "motor generator");

    // 波形: カウンタ 0 で HIGH、比較一致で LOW
    POCO_CHECK(mcpwm_generator_set_action_on_timer_event(m_gen[dir],
                 MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)),
      "motor timer action");
    POCO_CHECK(mcpwm_generator_set_action_on_compare_event(m_gen[dir],
                 MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, m_cmpr[dir], MCPWM_GEN_ACTION_LOW)),
      "motor compare action");

    // 初期状態: デューティ 0、出力を LOW に固定して停止
    POCO_CHECK(mcpwm_comparator_set_compare_value(m_cmpr[dir], 0), "motor initial duty");
    POCO_CHECK(mcpwm_generator_set_force_level(m_gen[dir], 0, true), "motor force low");
  }

  m_id      = id;
  m_percent = 0.0f;
  return true;
}

bool PocoMotor::run(float percent) {
  if (m_id >= POCOROBO_MOTOR_COUNT) {
    return false;
  }
  percent   = std::clamp(percent, -100.0f, 100.0f);
  m_percent = percent;

  // 速度を 0〜20000 ticks に換算。100 % は周期いっぱい(比較一致が起きず常時 HIGH)
  const auto ticks = static_cast<uint32_t>((std::abs(percent) / 100.0f) * kMcpwmPeriodTicks);

  // 動かす側。ticks が 0 なら停止(-1)
  int active = -1;
  if (ticks > 0) {
    active = (percent > 0.0f) ? kForward : kReverse;
  }

  // 先に動かさない側を LOW に固定してから、動かす側のデューティを設定して固定を解く
  for (int dir = 0; dir < 2; dir++) {
    if (dir != active) {
      POCO_CHECK(mcpwm_generator_set_force_level(m_gen[dir], 0, true), "motor force low");
    }
  }
  if (active >= 0) {
    POCO_CHECK(mcpwm_comparator_set_compare_value(m_cmpr[active], ticks), "motor duty");
    POCO_CHECK(mcpwm_generator_set_force_level(m_gen[active], -1, true), "motor release");
  }
  return true;
}
