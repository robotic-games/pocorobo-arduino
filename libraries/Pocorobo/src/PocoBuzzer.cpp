// ブザーの実装(MCPWM グループ 1 の独自タイマで周波数可変の矩形波を出す)
#include "Pocorobo.h"

#if POCOROBO_HAS_BUZZER

#include <algorithm>

#include "internal/PocoCheck.h"

namespace {
// 1 MHz 分解能。周期は tone() で周波数から決める
constexpr uint32_t kMcpwmResolutionHz  = 1000000;
constexpr uint32_t kInitialPeriodTicks = 1000;  // 初期値 1 kHz
constexpr int      kMcpwmGroup         = 1;     // モータはグループ 0 を主に使うので、こちらはグループ 1
}  // namespace

bool PocoBuzzer::begin() {
  mcpwm_timer_config_t timerConfig = {
    .group_id      = kMcpwmGroup,
    .clk_src       = MCPWM_TIMER_CLK_SRC_DEFAULT,
    .resolution_hz = kMcpwmResolutionHz,
    .count_mode    = MCPWM_TIMER_COUNT_MODE_UP,
    .period_ticks  = kInitialPeriodTicks,
    .intr_priority = 0,
    .flags         = {}};
  POCO_CHECK(mcpwm_new_timer(&timerConfig, &m_timer), "buzzer timer");

  mcpwm_operator_config_t operConfig = {
    .group_id      = kMcpwmGroup,
    .intr_priority = 0,
    .flags         = {}};
  POCO_CHECK(mcpwm_new_operator(&operConfig, &m_oper), "buzzer operator");
  POCO_CHECK(mcpwm_operator_connect_timer(m_oper, m_timer), "buzzer operator connect");

  mcpwm_comparator_config_t cmprConfig = {
    .intr_priority = 0,
    .flags         = {}};
  POCO_CHECK(mcpwm_new_comparator(m_oper, &cmprConfig, &m_cmpr), "buzzer comparator");

  mcpwm_generator_config_t genConfig = {
    .gen_gpio_num = PIN_BUZZER,
    .flags        = {}};
  POCO_CHECK(mcpwm_new_generator(m_oper, &genConfig, &m_gen), "buzzer generator");

  // 波形: カウンタ 0 で HIGH、比較一致で LOW
  POCO_CHECK(mcpwm_generator_set_action_on_timer_event(m_gen,
               MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)),
    "buzzer timer action");
  POCO_CHECK(mcpwm_generator_set_action_on_compare_event(m_gen,
               MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, m_cmpr, MCPWM_GEN_ACTION_LOW)),
    "buzzer compare action");

  // 初期状態: 出力を LOW に固定して無音
  POCO_CHECK(mcpwm_generator_set_force_level(m_gen, 0, true), "buzzer force low");

  POCO_CHECK(mcpwm_timer_enable(m_timer), "buzzer timer enable");
  POCO_CHECK(mcpwm_timer_start_stop(m_timer, MCPWM_TIMER_START_NO_STOP), "buzzer timer start");
  return true;
}

bool PocoBuzzer::tone(float hz) {
  if (m_gen == nullptr) {
    return false;
  }
  if (hz <= 0.0f) {
    return off();
  }

  hz   = std::clamp(hz, 100.0f, 10000.0f);
  m_hz = hz;

  // 周期 = 分解能 / 周波数、デューティ 50 %
  const auto period = static_cast<uint32_t>(kMcpwmResolutionHz / hz);
  POCO_CHECK(mcpwm_timer_set_period(m_timer, period), "buzzer period");
  POCO_CHECK(mcpwm_comparator_set_compare_value(m_cmpr, period / 2), "buzzer duty");

  // LOW 固定を解除して通常の PWM 出力に戻す
  POCO_CHECK(mcpwm_generator_set_force_level(m_gen, -1, true), "buzzer release");
  return true;
}

bool PocoBuzzer::off() {
  if (m_gen == nullptr) {
    return false;
  }
  m_hz = 0.0f;
  POCO_CHECK(mcpwm_generator_set_force_level(m_gen, 0, true), "buzzer force low");
  return true;
}

#endif  // POCOROBO_HAS_BUZZER
