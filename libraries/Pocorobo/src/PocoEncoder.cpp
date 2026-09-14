// エンコーダの実装(PCNT で直交 4 逓倍デコード)
#include "Pocorobo.h"

#if POCOROBO_HAS_ENCODER

#include "internal/PocoCheck.h"

namespace {
constexpr uint32_t kGlitchNs = 1000;    // 1 µs 未満のパルスはノイズとして無視
constexpr int      kPcntHigh = 32767;   // 16 bit カウンタの上限(ここで桁あふれを累積補償)
constexpr int      kPcntLow  = -32768;  // 16 bit カウンタの下限
}  // namespace

bool PocoEncoder::begin(uint8_t id) {
  // ユニット作成(桁あふれを累積補償する)
  pcnt_unit_config_t unitConfig = {
    .low_limit     = kPcntLow,
    .high_limit    = kPcntHigh,
    .intr_priority = 0,
    .flags         = {.accum_count = true},
  };
  POCO_CHECK(pcnt_new_unit(&unitConfig, &m_unit), "encoder unit");

  // グリッチフィルタ
  pcnt_glitch_filter_config_t filterConfig = {.max_glitch_ns = kGlitchNs};
  POCO_CHECK(pcnt_unit_set_glitch_filter(m_unit, &filterConfig), "encoder glitch filter");

  const int aPin = PIN_ENCODER_A[id];
  const int bPin = PIN_ENCODER_B[id];

  // チャンネル 0: エッジ = A 相、レベル = B 相
  pcnt_chan_config_t chanAConfig = {
    .edge_gpio_num  = aPin,
    .level_gpio_num = bPin,
    .flags          = {},
  };
  POCO_CHECK(pcnt_new_channel(m_unit, &chanAConfig, &m_chan[0]), "encoder channel A");

  // チャンネル 1: エッジ = B 相、レベル = A 相
  pcnt_chan_config_t chanBConfig = {
    .edge_gpio_num  = bPin,
    .level_gpio_num = aPin,
    .flags          = {},
  };
  POCO_CHECK(pcnt_new_channel(m_unit, &chanBConfig, &m_chan[1]), "encoder channel B");

  // 直交 4 逓倍デコード(ESP-IDF のロータリーエンコーダ例と同じ設定)
  POCO_CHECK(pcnt_channel_set_edge_action(m_chan[0], PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE), "encoder edge A");
  POCO_CHECK(pcnt_channel_set_level_action(m_chan[0], PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE), "encoder level A");
  POCO_CHECK(pcnt_channel_set_edge_action(m_chan[1], PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE), "encoder edge B");
  POCO_CHECK(pcnt_channel_set_level_action(m_chan[1], PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE), "encoder level B");

  // 桁あふれ補償には上下限をウォッチポイントに登録しておく必要がある
  POCO_CHECK(pcnt_unit_add_watch_point(m_unit, kPcntHigh), "encoder watch high");
  POCO_CHECK(pcnt_unit_add_watch_point(m_unit, kPcntLow), "encoder watch low");

  POCO_CHECK(pcnt_unit_enable(m_unit), "encoder enable");
  POCO_CHECK(pcnt_unit_clear_count(m_unit), "encoder clear");
  POCO_CHECK(pcnt_unit_start(m_unit), "encoder start");
  return true;
}

int PocoEncoder::count() const {
  if (m_unit == nullptr) {
    return 0;
  }
  int value = 0;
  pcnt_unit_get_count(m_unit, &value);
  return value;
}

void PocoEncoder::reset() {
  if (m_unit == nullptr) {
    return;
  }
  pcnt_unit_clear_count(m_unit);
}

#endif  // POCOROBO_HAS_ENCODER
