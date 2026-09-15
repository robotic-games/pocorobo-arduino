// 専用コントローラ(ESP-NOW 無線)受信の実装
//
// 無線の立ち上げは arduino-esp32 の WiFi(STA モード)に任せ、チャネル・ESP-NOW・受信窓は
// ESP-IDF の API を直接使う。省電力モードだけは WiFi.setSleep() を通す(arduino-esp32 が
// STA 開始イベントで WiFi.getSleep() の値を非同期に適用し直すため、IDF を直接呼ぶと上書きされる)。
#include "Pocorobo.h"

#include <WiFi.h>

#include <string.h>

#include "esp_mac.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/queue.h"
#include "internal/PocoControllerProtocol.h"
#include "nvs.h"

namespace proto = PocoControllerProtocol;

namespace {
// ペアリング情報の NVS 保存先(標準のプログラムと同じ名前空間・キー・形式)
constexpr char kNvsNamespace[]     = "pairing";
constexpr char kNvsControllerMac[] = "controllerMac";
constexpr char kNvsLmk[]           = "lmk";
constexpr char kNvsChannel[]       = "channel";
constexpr char kNvsVersion[]       = "version";

constexpr uint8_t kBroadcastMac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

constexpr int         kRecvQueueDepth = 16;
constexpr uint32_t    kTaskPeriodMs   = 10;
constexpr uint32_t    kTaskStackBytes = 8192;
constexpr UBaseType_t kTaskPriority   = 2;  // loop() のタスク(優先度 1)より 1 高い

// ビーコンを最初に受けてから相手を確定するまでの猶予(この間に 2 台目が見えたら失敗にする)
constexpr int64_t kCandidateConfirmUs = 1500 * 1000;
// PairCommit の再送間隔
constexpr int64_t kPairCommitRetransmitUs = 100 * 1000;
// 常時受信(受信窓を最大にして間欠受信を実質無効にする)
constexpr uint16_t kFullRxWakeWindowMs   = 65535;
constexpr uint16_t kFullRxWakeIntervalMs = 65535;

// 受信コールバックからタスクへ渡す 1 フレーム
struct RecvEvent {
  uint8_t mac[6];
  int     len;
  uint8_t data[sizeof(proto::InputFrame)];  // 最大フレームのサイズ
};

QueueHandle_t s_recvQueue = nullptr;

int64_t nowUs() {
  return esp_timer_get_time();
}

bool macEquals(const uint8_t* a, const uint8_t* b) {
  return memcmp(a, b, 6) == 0;
}

bool isZeroMac(const uint8_t mac[6]) {
  const uint8_t zero[6] = {};
  return macEquals(mac, zero);
}

bool isValidOperatingChannel(uint8_t channel) {
  for (const uint8_t candidate : proto::kOperatingChannels) {
    if (candidate == channel) {
      return true;
    }
  }
  return false;
}

void fillHeader(proto::FrameHeader& header, proto::FrameType type, uint32_t txId) {
  header.magic     = proto::kMagic;
  header.version   = proto::kProtocolVersion;
  header.frameType = static_cast<uint8_t>(type);
  header.txId      = txId;
}

// peer を登録する(登録済みなら設定を置き換える)。lmk が nullptr なら鍵を持たない peer
esp_err_t addOrReplacePeer(const uint8_t mac[6], const uint8_t* lmk, uint8_t channel, bool encrypt) {
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  if (lmk != nullptr) {
    memcpy(peer.lmk, lmk, proto::kLmkLen);
  }
  peer.channel = channel;
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt = encrypt;

  esp_err_t err = esp_now_add_peer(&peer);
  if (err == ESP_ERR_ESPNOW_EXIST) {
    err = esp_now_mod_peer(&peer);
  }
  if (err != ESP_OK) {
    log_e("peer 登録失敗 encrypt=%d ch=%u: %s", encrypt ? 1 : 0, channel, esp_err_to_name(err));
  }
  return err;
}

// ESP-NOW の受信コールバック(Wi-Fi タスクで呼ばれる)。軽く検査してキューへ積むだけ
void onEspNowReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (s_recvQueue == nullptr || info == nullptr || info->src_addr == nullptr || data == nullptr ||
      len <= 0 || len > static_cast<int>(sizeof(RecvEvent::data))) {
    return;
  }
  if (!proto::isValidHeader(data, len)) {
    return;
  }

  RecvEvent event = {};
  memcpy(event.mac, info->src_addr, sizeof(event.mac));
  event.len = len;
  memcpy(event.data, data, static_cast<size_t>(len));
  (void)xQueueSend(s_recvQueue, &event, 0);
}
}  // namespace

// ---- 起動・タスク --------------------------------------------------------

bool PocoController::begin() {
  static_assert(sizeof(Bond::lmk) == proto::kLmkLen, "Bond::lmk の長さがプロトコルと違う");
  if (m_task != nullptr) {
    return true;
  }

  const esp_err_t macErr = esp_read_mac(m_robotMac, ESP_MAC_WIFI_STA);
  if (macErr != ESP_OK) {
    log_e("MAC 読み取り失敗: %s", esp_err_to_name(macErr));
    return false;
  }

  m_hasBond.store(loadBond(m_bond));

  s_recvQueue = xQueueCreate(kRecvQueueDepth, sizeof(RecvEvent));
  if (s_recvQueue == nullptr) {
    log_e("controller: 受信キューを作れません");
    m_hasBond.store(false);
    return false;
  }

  const BaseType_t created = xTaskCreateUniversal(
    taskEntry, "PocoController", kTaskStackBytes, this, kTaskPriority, &m_task, ARDUINO_RUNNING_CORE);
  if (created != pdPASS) {
    log_e("controller: 受信タスクを作れません");
    vQueueDelete(s_recvQueue);
    s_recvQueue = nullptr;
    m_task      = nullptr;
    m_hasBond.store(false);
    return false;
  }
  return true;
}

void PocoController::taskEntry(void* arg) {
  auto* self = static_cast<PocoController*>(arg);
  for (;;) {
    self->update();
    vTaskDelay(pdMS_TO_TICKS(kTaskPeriodMs));
  }
}

// 10 ms ごとに 1 回。指示の反映 → 無線状態の見直し → 受信処理 → ペアリングの進行
void PocoController::update() {
  const int64_t now = nowUs();
  applyCommand();
  const RfState desired = desiredRfState(now);
  if (desired != m_rfTarget) {
    enterRfState(desired);
  }
  drainRecvQueue();
  tickPairing(now);
}

// ---- スケッチからの操作 ------------------------------------------------------

void PocoController::startPairing() {
  setCommand(Command::StartPairing);
}

void PocoController::cancelPairing() {
  setCommand(Command::CancelPairing);
}

void PocoController::unpair() {
  setCommand(Command::Unpair);
}

bool PocoController::isConnected() const {
  return isConnected(snapshot(), nowUs());
}

PocoController::State PocoController::state() const {
  if (m_pairingActive.load()) {
    return State::Pairing;
  }
  if (isConnected()) {
    return State::Connected;
  }
  if (m_pairingFailed.load()) {
    return State::PairingFailed;
  }
  return m_hasBond.load() ? State::Disconnected : State::Unpaired;
}

int PocoController::axis(uint8_t id) const {
  const Snapshot s = snapshot();
  if (id > 1 || !isConnected(s, nowUs())) {
    return 0;
  }
  // -127..127 → -100..100(四捨五入)
  const int raw    = (id == 0) ? s.axisX : s.axisY;
  const int scaled = (raw * 100 + (raw >= 0 ? 63 : -63)) / 127;
  return (scaled > 100) ? 100 : (scaled < -100) ? -100 : scaled;
}

bool PocoController::button(uint8_t id) const {
  const Snapshot s = snapshot();
  if (id > 15 || !isConnected(s, nowUs())) {
    return false;
  }
  return ((s.buttons >> id) & 0x1U) != 0U;
}

bool PocoController::batteryLow() const {
  const Snapshot s = snapshot();
  if (!isConnected(s, nowUs())) {
    return false;
  }
  return (s.flags & proto::kFlagBatteryLow) != 0;
}

PocoController::Snapshot PocoController::snapshot() const {
  portENTER_CRITICAL(&m_mux);
  const Snapshot s = m_snapshot;
  portEXIT_CRITICAL(&m_mux);
  return s;
}

bool PocoController::isConnected(const Snapshot& s, int64_t now) const {
  return m_hasBond.load() && s.lastRxUs != 0 &&
         now - s.lastRxUs <= static_cast<int64_t>(proto::kInputTimeoutUs);
}

// ---- 指示の反映(受信タスク内) ------------------------------------------------

void PocoController::applyCommand() {
  const auto command = static_cast<Command>(m_command.exchange(0));
  switch (command) {
    case Command::StartPairing:
      doStartPairing();
      break;
    case Command::CancelPairing:
      doCancelPairing();
      break;
    case Command::Unpair:
      doUnpair(true);
      break;
    case Command::UnpairFromRemote:
      doUnpair(false);
      break;
    case Command::None:
      break;
  }
}

void PocoController::doStartPairing() {
  clearPairingRuntime();
  m_pairingFailed.store(false);
  m_phase            = Phase::CollectBeacon;
  m_pairingStartedUs = nowUs();
  m_pairingActive.store(true);
  if (m_rfState == RfState::Pairing) {
    // ペアリング中にやり直された場合。無線の状態は変わらないので、ここで探索チャネルへ戻す
    configurePairingRf();
  }
  log_i("controller: ペアリング開始。コントローラのペアリングボタンを押してください");
}

void PocoController::doCancelPairing() {
  m_pairingActive.store(false);
  clearPairingRuntime();
  m_pairingFailed.store(false);
}

// notifyPeer = true: スケッチ起点。無線を止める前に相手へ解除を通知する
// notifyPeer = false: 相手からの通知起点。送り返さず(ループ防止)こちらの情報を消すだけ
void PocoController::doUnpair(bool notifyPeer) {
  if (notifyPeer) {
    sendUnpairNotice();
  }
  m_pairingActive.store(false);
  clearPairingRuntime();
  m_pairingFailed.store(false);
  eraseBond();

  const Bond oldBond = m_bond;
  m_hasBond.store(false);
  m_bond = Bond();
  if (m_espNowStarted && !isZeroMac(oldBond.mac)) {
    (void)esp_now_del_peer(oldBond.mac);
  }

  portENTER_CRITICAL(&m_mux);
  m_snapshot = Snapshot();
  portEXIT_CRITICAL(&m_mux);

  enterRfState(RfState::Off);
  log_i("controller: ペアリング情報を消しました");
}

void PocoController::clearPairingRuntime() {
  memset(m_candidateMac, 0, sizeof(m_candidateMac));
  m_candidateCount   = 0;
  m_pendingBond      = Bond();
  m_pendingTxId      = 0;
  m_pairingStartedUs = 0;
  m_firstCandidateUs = 0;
  m_assignSentCount  = 0;
  m_switchSentCount  = 0;
  m_lastAssignSentUs = 0;
  m_lastCommitSentUs = 0;
  m_lastSwitchSentUs = 0;
  m_pairCommitSentUs = 0;
  m_switchAtUs       = 0;
  m_phase            = Phase::None;
}

void PocoController::failPairing(const char* reason) {
  (void)reason;  // ログを無効にしたビルドでは使われない
  m_pairingActive.store(false);
  clearPairingRuntime();
  m_pairingFailed.store(true);
  log_w("controller: ペアリング失敗: %s", reason);
}

// ---- 無線の状態 --------------------------------------------------------------

PocoController::RfState PocoController::desiredRfState(int64_t now) const {
  if (m_pairingActive.load()) {
    return RfState::Pairing;
  }
  if (!m_hasBond.load()) {
    return RfState::Off;
  }
  const int64_t lastRxUs = snapshot().lastRxUs;
  if (lastRxUs != 0 && now - lastRxUs <= static_cast<int64_t>(proto::kDrivingIdleTimeoutUs)) {
    return RfState::Driving;
  }
  return RfState::Standby;
}

void PocoController::enterRfState(RfState state) {
  m_rfTarget = state;
  if (state == RfState::Off) {
    stopRf();
    m_rfState = RfState::Off;
    return;
  }

  if (!ensureRfStarted()) {
    m_pairingActive.store(false);
    clearPairingRuntime();
    m_pairingFailed.store(true);
    stopRf();
    m_rfState  = RfState::Off;
    m_rfTarget = RfState::Off;
    return;
  }

  switch (state) {
    case RfState::Pairing:
      configurePairingRf();
      break;
    case RfState::Standby:
      // 未接続の STA の間欠受信(connectionless power save)。受信窓は待機用の契約値
      configureBondedRf(m_bond.channel);
      configureWakeWindow(proto::kStandbyWakeWindowMs, proto::kStandbyWakeIntervalMs, true);
      break;
    case RfState::Driving:
      configureBondedRf(m_bond.channel);
      configureWakeWindow(kFullRxWakeWindowMs, kFullRxWakeIntervalMs, false);
      break;
    case RfState::Off:
      break;
  }
  m_rfState = state;
}

bool PocoController::ensureRfStarted() {
  if (m_rfStarted) {
    return true;
  }

  WiFi.persistent(false);  // Wi-Fi の設定をフラッシュに書かない
  WiFi.setSleep(false);    // 常時受信で始める(省電力は待機状態に入るときに設定する)
  if (!WiFi.mode(WIFI_STA)) {
    log_e("controller: Wi-Fi を開始できません");
    return false;
  }
  m_rfStarted = true;

  esp_err_t err = esp_now_init();
  if (err == ESP_OK) {
    err = esp_now_set_pmk(proto::kPrimaryMasterKey);
  }
  if (err == ESP_OK) {
    err = esp_now_register_recv_cb(onEspNowReceive);
  }
  if (err != ESP_OK) {
    log_e("controller: ESP-NOW を開始できません: %s", esp_err_to_name(err));
    stopRf();
    return false;
  }

  m_espNowStarted = true;
  log_i("controller: 無線を開始しました");
  return true;
}

void PocoController::stopRf() {
  if (m_espNowStarted) {
    m_espNowStarted = false;
    (void)esp_now_unregister_recv_cb();
    (void)esp_now_deinit();
  }
  if (m_rfStarted) {
    m_rfStarted = false;
    (void)WiFi.mode(WIFI_OFF);
    m_currentChannel     = 0;
    m_lastWakeWindowMs   = 0;
    m_lastWakeIntervalMs = 0;
    m_lastPowerSave      = false;
    log_i("controller: 無線を停止しました");
  }
}

// ペアリング用: チャネル 1 で常時受信し、ブロードキャスト(ビーコン)を受ける
void PocoController::configurePairingRf() {
  if (m_currentChannel != proto::kPairingChannel) {
    setChannel(proto::kPairingChannel);
  }
  configureWakeWindow(kFullRxWakeWindowMs, kFullRxWakeIntervalMs, false);
  (void)addOrReplacePeer(kBroadcastMac, nullptr, proto::kPairingChannel, false);
}

// ペアリング済み用: 運用チャネルに移り、相手を暗号化 peer として登録する
void PocoController::configureBondedRf(uint8_t channel) {
  if (!m_hasBond.load()) {
    return;
  }
  if (m_currentChannel != channel) {
    setChannel(channel);
  }
  (void)addOrReplacePeer(m_bond.mac, m_bond.lmk, channel, true);
}

// 受信窓(間欠受信)と省電力モードを設定する。直近と同じ設定なら何もしない
void PocoController::configureWakeWindow(uint16_t windowMs, uint16_t intervalMs, bool powerSave) {
  if (m_lastWakeWindowMs == windowMs && m_lastWakeIntervalMs == intervalMs && m_lastPowerSave == powerSave) {
    return;
  }

  esp_err_t err = WiFi.setSleep(powerSave) ? ESP_OK : ESP_FAIL;
  if (err == ESP_OK) {
    err = esp_now_set_wake_window(windowMs);
  }
  if (err == ESP_OK) {
    err = esp_wifi_connectionless_module_set_wake_interval(intervalMs);
  }
  if (err != ESP_OK) {
    log_w("controller: 省電力設定失敗 window=%u interval=%u: %s", windowMs, intervalMs, esp_err_to_name(err));
    return;
  }

  m_lastWakeWindowMs   = windowMs;
  m_lastWakeIntervalMs = intervalMs;
  m_lastPowerSave      = powerSave;
}

void PocoController::setChannel(uint8_t channel) {
  const esp_err_t err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  if (err != ESP_OK) {
    log_w("controller: チャネル変更失敗 ch=%u: %s", channel, esp_err_to_name(err));
    return;
  }
  m_currentChannel = channel;
}

// ---- 受信処理 ----------------------------------------------------------------

void PocoController::drainRecvQueue() {
  if (s_recvQueue == nullptr) {
    return;
  }

  RecvEvent event;
  while (xQueueReceive(s_recvQueue, &event, 0) == pdTRUE) {
    proto::FrameHeader header;
    memcpy(&header, event.data, sizeof(header));
    switch (static_cast<proto::FrameType>(header.frameType)) {
      case proto::FrameType::Beacon:
        processBeacon(event.mac, event.data, event.len);
        break;
      case proto::FrameType::PairAck:
        processPairAck(event.mac, event.data, event.len);
        break;
      case proto::FrameType::CommitAck:
        processCommitAck(event.mac, event.data, event.len);
        break;
      case proto::FrameType::Input:
        processInput(event.mac, event.data, event.len);
        break;
      case proto::FrameType::Unpair:
        processUnpair(event.mac, event.len);
        break;
      default:
        break;
    }
  }
}

// ビーコン: ペアリングボタンが押されたコントローラを候補にする。2 台以上見えたら失敗
void PocoController::processBeacon(const uint8_t* mac, const uint8_t* data, int len) {
  if (!m_pairingActive.load() || m_phase != Phase::CollectBeacon ||
      len != static_cast<int>(sizeof(proto::BeaconFrame))) {
    return;
  }

  proto::BeaconFrame frame;
  memcpy(&frame, data, sizeof(frame));
  if (frame.deviceType != proto::kDeviceTypeController ||
      (frame.flags & proto::kBeaconFlagPairingRequest) == 0) {
    return;
  }

  if (m_candidateCount == 0) {
    memcpy(m_candidateMac, mac, sizeof(m_candidateMac));
    m_candidateCount   = 1;
    m_firstCandidateUs = nowUs();
    return;
  }
  if (macEquals(m_candidateMac, mac)) {
    return;
  }
  m_candidateCount = 2;
  failPairing("複数のコントローラが押されました。1 台だけ押して再試行");
}

// PairAck: 相手が鍵を受け取った。ここでペアリング情報を確定・保存し、PairCommit を送る
void PocoController::processPairAck(const uint8_t* mac, const uint8_t* data, int len) {
  if (!m_pairingActive.load() || m_phase != Phase::AwaitPairAck ||
      len != static_cast<int>(sizeof(proto::PairAckFrame)) || !macEquals(mac, m_pendingBond.mac)) {
    return;
  }

  proto::PairAckFrame frame;
  memcpy(&frame, data, sizeof(frame));
  if (frame.header.txId != m_pendingTxId) {
    return;
  }

  if (!saveBond(m_pendingBond)) {
    failPairing("ペアリング情報を保存できません");
    return;
  }
  m_bond = m_pendingBond;
  m_hasBond.store(true);

  sendPairCommit();
  m_pairCommitSentUs = nowUs();
  m_lastCommitSentUs = m_pairCommitSentUs;
  m_phase            = Phase::AwaitCommitAck;
}

// CommitAck: 相手が運用チャネルを了解した。SwitchConfirm の送信へ
void PocoController::processCommitAck(const uint8_t* mac, const uint8_t* data, int len) {
  if (!m_pairingActive.load() || m_phase != Phase::AwaitCommitAck ||
      len != static_cast<int>(sizeof(proto::CommitAckFrame)) || !macEquals(mac, m_bond.mac)) {
    return;
  }

  proto::CommitAckFrame frame;
  memcpy(&frame, data, sizeof(frame));
  if (frame.header.txId != m_pendingTxId) {
    return;
  }

  m_switchSentCount  = 0;
  m_lastSwitchSentUs = 0;
  m_switchAtUs       = 0;
  m_phase            = Phase::SendSwitch;
}

// 定常入力: 最新の入力を写しに保存する。ペアリング中なら最初の入力で完了
void PocoController::processInput(const uint8_t* mac, const uint8_t* data, int len) {
  if (!m_hasBond.load() || len != static_cast<int>(sizeof(proto::InputFrame)) ||
      !macEquals(mac, m_bond.mac)) {
    return;
  }

  proto::InputFrame frame;
  memcpy(&frame, data, sizeof(frame));
  if (frame.input.version != proto::kProtocolVersion) {
    return;
  }

  const int64_t now = nowUs();
  portENTER_CRITICAL(&m_mux);
  m_snapshot.lastRxUs = now;
  m_snapshot.buttons  = frame.input.buttons;
  m_snapshot.axisX    = frame.input.axisX;
  m_snapshot.axisY    = frame.input.axisY;
  m_snapshot.flags    = frame.input.flags;
  portEXIT_CRITICAL(&m_mux);

  if (m_pairingActive.load() && m_phase == Phase::AwaitInput) {
    m_pairingActive.store(false);
    m_phase = Phase::None;
    m_pairingFailed.store(false);
    log_i("controller: ペアリングしました");
  }
}

// 相手からの解除通知。実際の消去は次回の update() 冒頭で行う(受信処理の途中で無線を止めない)
void PocoController::processUnpair(const uint8_t* mac, int len) {
  if (!m_hasBond.load() || len != static_cast<int>(sizeof(proto::UnpairFrame)) ||
      !macEquals(mac, m_bond.mac)) {
    return;
  }
  log_i("controller: コントローラから解除通知を受信");
  setCommand(Command::UnpairFromRemote);
}

// ---- ペアリングの進行 --------------------------------------------------------

void PocoController::tickPairing(int64_t now) {
  if (!m_pairingActive.load()) {
    return;
  }

  if (m_pairingStartedUs != 0 &&
      now - m_pairingStartedUs > static_cast<int64_t>(proto::kPairingTimeoutMs) * 1000) {
    failPairing("タイムアウト");
    return;
  }

  switch (m_phase) {
    case Phase::CollectBeacon:
      // 1 台だけが猶予時間の間見え続けたら、その相手に決める
      if (m_candidateCount == 1 && m_firstCandidateUs != 0 && now - m_firstCandidateUs >= kCandidateConfirmUs) {
        beginPairAssign();
      }
      break;
    case Phase::SendAssign:
      tickPairAssignBurst(now);
      break;
    case Phase::AwaitCommitAck:
      tickPairCommitRetransmit(now);
      break;
    case Phase::SendSwitch:
      tickSwitchConfirm(now);
      break;
    case Phase::AwaitPairAck:
    case Phase::AwaitInput:
    case Phase::None:
      break;
  }
}

// 相手を決めて鍵と運用チャネルを作り、平文 peer として登録する
void PocoController::beginPairAssign() {
  const uint8_t channel = selectOperatingChannel();
  setChannel(proto::kPairingChannel);

  m_pendingBond = Bond();
  memcpy(m_pendingBond.mac, m_candidateMac, sizeof(m_pendingBond.mac));
  esp_fill_random(m_pendingBond.lmk, sizeof(m_pendingBond.lmk));
  m_pendingBond.channel = channel;

  uint32_t txId = esp_random();
  if (txId == 0) {
    txId = 1;
  }
  m_pendingTxId = txId;

  if (addOrReplacePeer(m_pendingBond.mac, nullptr, proto::kPairingChannel, false) != ESP_OK) {
    failPairing("コントローラを peer に登録できません");
    return;
  }

  m_assignSentCount  = 0;
  m_lastAssignSentUs = 0;
  m_phase            = Phase::SendAssign;
}

// 運用チャネルは自分の MAC のハッシュで候補から選ぶ(周辺スキャンは他のペアを観測できず、時間もかかるため)
uint8_t PocoController::selectOperatingChannel() const {
  uint32_t macHash = 0;
  for (const uint8_t byte : m_robotMac) {
    macHash += byte;
  }
  return proto::kOperatingChannels[macHash % proto::kOperatingChannelCount];
}

// PairAssign(平文)を一定間隔で規定回数送る。送り終えたら暗号化 peer に切り替えて PairAck を待つ
void PocoController::tickPairAssignBurst(int64_t now) {
  if (m_assignSentCount >= proto::kPairAssignBurstCount) {
    if (addOrReplacePeer(m_pendingBond.mac, m_pendingBond.lmk, proto::kPairingChannel, true) == ESP_OK) {
      m_phase = Phase::AwaitPairAck;
    } else {
      failPairing("暗号化 peer へ切り替えできません");
    }
    return;
  }

  if (m_lastAssignSentUs != 0 &&
      now - m_lastAssignSentUs < static_cast<int64_t>(proto::kPairAssignBurstIntervalMs) * 1000) {
    return;
  }

  proto::PairAssignFrame frame = {};
  fillHeader(frame.header, proto::FrameType::PairAssign, m_pendingTxId);
  memcpy(frame.robotMac, m_robotMac, sizeof(frame.robotMac));
  memcpy(frame.lmk, m_pendingBond.lmk, sizeof(frame.lmk));
  frame.assignedChannel = m_pendingBond.channel;
  frame.reserved        = 0;

  const esp_err_t err = esp_now_send(m_pendingBond.mac, reinterpret_cast<const uint8_t*>(&frame), sizeof(frame));
  if (err != ESP_OK) {
    log_w("controller: PairAssign 送信失敗: %s", esp_err_to_name(err));
  }
  m_assignSentCount++;
  m_lastAssignSentUs = now;
}

void PocoController::sendPairCommit() {
  proto::PairCommitFrame frame = {};
  fillHeader(frame.header, proto::FrameType::PairCommit, m_pendingTxId);
  frame.assignedChannel = m_bond.channel;
  frame.reserved        = 0;

  const esp_err_t err = esp_now_send(m_bond.mac, reinterpret_cast<const uint8_t*>(&frame), sizeof(frame));
  if (err != ESP_OK) {
    log_w("controller: PairCommit 送信失敗: %s", esp_err_to_name(err));
  }
}

// PairCommit を再送しつつ CommitAck を待つ。一定時間来なければ運用チャネルへ移ってしまう
void PocoController::tickPairCommitRetransmit(int64_t now) {
  if (m_pairCommitSentUs != 0 &&
      now - m_pairCommitSentUs > static_cast<int64_t>(proto::kCommitParkFallbackMs) * 1000) {
    log_w("controller: CommitAck 未受信のため運用チャネルへ移ります");
    parkOnAssignedChannel();
    return;
  }

  if (m_lastCommitSentUs != 0 && now - m_lastCommitSentUs < kPairCommitRetransmitUs) {
    return;
  }
  sendPairCommit();
  m_lastCommitSentUs = now;
}

// SwitchConfirm を短い間隔で規定回数送り、最初の送信から遅延時間が過ぎたら運用チャネルへ移る
void PocoController::tickSwitchConfirm(int64_t now) {
  if (m_switchSentCount < proto::kSwitchConfirmRetransmitCount &&
      (m_lastSwitchSentUs == 0 ||
        now - m_lastSwitchSentUs >= static_cast<int64_t>(proto::kSwitchConfirmRetransmitIntervalMs) * 1000)) {
    proto::SwitchConfirmFrame frame = {};
    fillHeader(frame.header, proto::FrameType::SwitchConfirm, m_pendingTxId);
    frame.switchDelayMs = proto::kSwitchDelayMs;
    frame.reserved      = 0;

    const esp_err_t err = esp_now_send(m_bond.mac, reinterpret_cast<const uint8_t*>(&frame), sizeof(frame));
    if (err != ESP_OK) {
      log_w("controller: SwitchConfirm 送信失敗: %s", esp_err_to_name(err));
    }
    m_switchSentCount++;
    m_lastSwitchSentUs = now;
    if (m_switchAtUs == 0) {
      m_switchAtUs = now + static_cast<int64_t>(proto::kSwitchDelayMs) * 1000;
    }
  }

  if (m_switchAtUs != 0 && now >= m_switchAtUs) {
    parkOnAssignedChannel();
  }
}

void PocoController::parkOnAssignedChannel() {
  if (m_bond.channel == 0 || !m_hasBond.load()) {
    failPairing("運用チャネルへ移動できません");
    return;
  }
  configureBondedRf(m_bond.channel);
  m_phase = Phase::AwaitInput;
}

// 解除を相手へベストエフォートで通知する(暗号化ユニキャスト)。相手は常時受信なので連続送信でよい
void PocoController::sendUnpairNotice() {
  if (!m_hasBond.load() || m_rfState == RfState::Off) {
    return;
  }
  proto::UnpairFrame frame = {};
  fillHeader(frame.header, proto::FrameType::Unpair, 0);
  for (int i = 0; i < proto::kUnpairRobotBurstCount; ++i) {
    const esp_err_t err = esp_now_send(m_bond.mac, reinterpret_cast<const uint8_t*>(&frame), sizeof(frame));
    if (err != ESP_OK) {
      log_w("controller: 解除通知の送信失敗: %s", esp_err_to_name(err));
      break;
    }
  }
}

// ---- NVS ---------------------------------------------------------------------

bool PocoController::loadBond(Bond& out) const {
  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return false;  // 名前空間が無い(未ペアリング)のは正常
  }

  uint8_t    version = 0;
  uint8_t    channel = 0;
  size_t     macLen  = sizeof(out.mac);
  size_t     lmkLen  = sizeof(out.lmk);
  const bool ok      = nvs_get_u8(handle, kNvsVersion, &version) == ESP_OK &&
                  version == proto::kProtocolVersion &&
                  nvs_get_blob(handle, kNvsControllerMac, out.mac, &macLen) == ESP_OK &&
                  macLen == sizeof(out.mac) &&
                  nvs_get_blob(handle, kNvsLmk, out.lmk, &lmkLen) == ESP_OK &&
                  lmkLen == sizeof(out.lmk) &&
                  nvs_get_u8(handle, kNvsChannel, &channel) == ESP_OK &&
                  isValidOperatingChannel(channel);
  nvs_close(handle);

  if (ok) {
    out.channel = channel;
  }
  return ok;
}

bool PocoController::saveBond(const Bond& bond) const {
  nvs_handle_t handle = 0;
  esp_err_t    err    = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    log_e("controller: NVS を開けません: %s", esp_err_to_name(err));
    return false;
  }

  err = nvs_set_u8(handle, kNvsVersion, proto::kProtocolVersion);
  if (err == ESP_OK) {
    err = nvs_set_blob(handle, kNvsControllerMac, bond.mac, sizeof(bond.mac));
  }
  if (err == ESP_OK) {
    err = nvs_set_blob(handle, kNvsLmk, bond.lmk, sizeof(bond.lmk));
  }
  if (err == ESP_OK) {
    err = nvs_set_u8(handle, kNvsChannel, bond.channel);
  }
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);

  if (err != ESP_OK) {
    log_e("controller: ペアリング情報の保存失敗: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

void PocoController::eraseBond() const {
  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
    return;
  }
  const esp_err_t eraseErr  = nvs_erase_all(handle);
  const esp_err_t commitErr = nvs_commit(handle);
  nvs_close(handle);
  if (eraseErr != ESP_OK || commitErr != ESP_OK) {
    log_w("controller: ペアリング情報の消去失敗 erase=%s commit=%s", esp_err_to_name(eraseErr), esp_err_to_name(commitErr));
  }
}
