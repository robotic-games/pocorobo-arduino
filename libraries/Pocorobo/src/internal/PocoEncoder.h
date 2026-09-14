// エンコーダ 1 ch(PCNT で直交 4 逓倍デコード)
#pragma once

#include <Arduino.h>

#include "driver/pulse_cnt.h"

class PocoEncoder {
public:
  // 累積カウント(符号付き、reset() からの相対値)。未初期化なら 0
  // 16 bit カウンタの桁あふれはドライバが累積補償する
  int count() const;

  // カウントを 0 に戻す
  void reset();

private:
  friend class PocoDevices;
  bool begin(uint8_t id);

  pcnt_unit_handle_t    m_unit    = nullptr;
  pcnt_channel_handle_t m_chan[2] = {nullptr, nullptr};
};
