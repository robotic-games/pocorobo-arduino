// 競技システム ATTRACTS のトランシーバ受信の実装
//
// UART コネクタの RX を UART1 に受信専用で割り当て、専用タスクが受信バイトを
// フレームパーサへ流して最新の操縦者入力・機体状態を写しに保つ。
#include "Pocorobo.h"

#if defined(POCOROBO_BOARD_STANDARD)

#include "driver/uart.h"
#include "esp_timer.h"
#include "internal/PocoCheck.h"

namespace proto = PocoAttractsProtocol;

namespace {
constexpr uart_port_t kUartPort = UART_NUM_1;
constexpr int         kBaudRate = 115200;

// 受信リングバッファ。50 Hz で 2 フレーム(最大 142 バイト)が届くので十分な余裕を取る。
// ESP-IDF は UART FIFO 長(128)より大きいことを要求する
constexpr int kRxBufferSize = 512;

// 1 回の読み出しで受け取る最大バイト数と、届かないときに読み出しを諦めるまでの待ち時間
constexpr size_t   kReadChunkSize = 128;
constexpr uint32_t kReadTimeoutMs = 20;

constexpr uint32_t    kTaskStackBytes = 4096;
constexpr UBaseType_t kTaskPriority   = 2;  // loop() のタスク(優先度 1)より 1 高い

// 未接続とみなすまでの無受信時間(50 Hz の 10 フレーム分)
constexpr int64_t kLinkTimeoutUs = 200 * 1000;

int64_t nowUs() {
  return esp_timer_get_time();
}

// 累積値の差分(桁あふれしても正しい差分になるよう、引き算は uint32 で行う)
int32_t wrapDiff(int32_t now, int32_t base) {
  return static_cast<int32_t>(static_cast<uint32_t>(now) - static_cast<uint32_t>(base));
}
}  // namespace

bool PocoAttracts::begin() {
  if (m_task != nullptr) {
    return true;
  }

  // 受信専用のため送信バッファは 0、イベントキューも使わない
  POCO_CHECK(uart_driver_install(kUartPort, kRxBufferSize, 0, 0, nullptr, 0), "attracts uart driver");

  uart_config_t config = {};
  config.baud_rate     = kBaudRate;
  config.data_bits     = UART_DATA_8_BITS;
  config.parity        = UART_PARITY_DISABLE;
  config.stop_bits     = UART_STOP_BITS_1;
  config.flow_ctrl     = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk    = UART_SCLK_DEFAULT;
  if (uart_param_config(kUartPort, &config) != ESP_OK) {
    log_e("attracts uart config failed");
    uart_driver_delete(kUartPort);
    return false;
  }

  // RX だけ割り当てる(TX / RTS / CTS は触らない)
  if (uart_set_pin(kUartPort, UART_PIN_NO_CHANGE, RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
    log_e("attracts uart pin failed");
    uart_driver_delete(kUartPort);
    return false;
  }

  const BaseType_t created = xTaskCreateUniversal(
    taskEntry, "PocoAttracts", kTaskStackBytes, this, kTaskPriority, &m_task, ARDUINO_RUNNING_CORE);
  if (created != pdPASS) {
    log_e("attracts: 受信タスクを作れません");
    uart_driver_delete(kUartPort);
    m_task = nullptr;
    return false;
  }
  return true;
}

void PocoAttracts::taskEntry(void* arg) {
  static_cast<PocoAttracts*>(arg)->receiveLoop();
}

void PocoAttracts::receiveLoop() {
  uint8_t chunk[kReadChunkSize];
  for (;;) {
    // 届いた分だけ読む(何も届かなければ kReadTimeoutMs で戻ってくる)
    const int read = uart_read_bytes(kUartPort, chunk, sizeof(chunk), pdMS_TO_TICKS(kReadTimeoutMs));
    for (int i = 0; i < read; ++i) {
      switch (m_parser.feed(chunk[i])) {
        case proto::FrameKind::Input: {
          const proto::Input& input = m_parser.lastInput();
          const int64_t       now   = nowUs();
          portENTER_CRITICAL(&m_mux);
          m_snapshot.keyBits = input.keyBits;
          // 取りこぼしを作らないよう、受信側は単調に増える累積だけを持つ(差分は読む側が計算する)。
          // 桁あふれは自然に一周させるため、符号付きオーバーフローを避けて uint32 で加算する
          m_snapshot.accumX = static_cast<int32_t>(static_cast<uint32_t>(m_snapshot.accumX) +
                                                   static_cast<uint32_t>(input.mouseDeltaX));
          m_snapshot.accumY = static_cast<int32_t>(static_cast<uint32_t>(m_snapshot.accumY) +
                                                   static_cast<uint32_t>(input.mouseDeltaY));
          m_snapshot.lastInputUs = now;
          portEXIT_CRITICAL(&m_mux);
          break;
        }
        case proto::FrameKind::Robot: {
          const proto::Robot& robot = m_parser.lastRobot();
          portENTER_CRITICAL(&m_mux);
          m_snapshot.robot = robot;
          portEXIT_CRITICAL(&m_mux);
          break;
        }
        case proto::FrameKind::None:
          break;
      }
    }
  }
}

PocoAttracts::Snapshot PocoAttracts::snapshot() const {
  portENTER_CRITICAL(&m_mux);
  const Snapshot s = m_snapshot;
  portEXIT_CRITICAL(&m_mux);
  return s;
}

bool PocoAttracts::isConnected() const {
  const Snapshot s = snapshot();
  return s.lastInputUs != 0 && (nowUs() - s.lastInputUs) <= kLinkTimeoutUs;
}

bool PocoAttracts::key(Key key) const {
  const int keyId = static_cast<int>(key);
  if (!proto::isValidKeyId(keyId)) {
    return false;
  }
  // 未接続時は全キー離した扱い(押しっぱなしの残留を防ぐ)
  const Snapshot s = snapshot();
  if (s.lastInputUs == 0 || (nowUs() - s.lastInputUs) > kLinkTimeoutUs) {
    return false;
  }
  return ((s.keyBits >> keyId) & 1ULL) != 0;
}

int PocoAttracts::mouseDeltaX() {
  const int32_t accum = snapshot().accumX;
  const int32_t diff  = wrapDiff(accum, m_lastReadX);
  m_lastReadX         = accum;
  return diff;
}

int PocoAttracts::mouseDeltaY() {
  const int32_t accum = snapshot().accumY;
  const int32_t diff  = wrapDiff(accum, m_lastReadY);
  m_lastReadY         = accum;
  return diff;
}

int PocoAttracts::mouseTotalX() const {
  return snapshot().accumX;
}

int PocoAttracts::mouseTotalY() const {
  return snapshot().accumY;
}

int PocoAttracts::hp() const {
  return snapshot().robot.hp;
}

int PocoAttracts::maxHp() const {
  return snapshot().robot.maxHp;
}

int PocoAttracts::heat() const {
  return snapshot().robot.heat;
}

int PocoAttracts::maxHeat() const {
  return snapshot().robot.maxHeat;
}

int PocoAttracts::team() const {
  return snapshot().robot.team;
}

int PocoAttracts::role() const {
  return snapshot().robot.role;
}

int PocoAttracts::bulletSpeedLimit() const {
  return snapshot().robot.bulletSpeedLimit;
}

#endif  // POCOROBO_BOARD_STANDARD
