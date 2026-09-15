// 専用コントローラ(ESP-NOW 無線)の受信
//
// ライブラリ内の専用タスク(10 ms 周期)が受信キューの処理・ペアリングの進行・接続監視を行う。
// スケッチ側で定期的に呼ぶものはない。axis() / button() は最新の入力の写しを読むだけで、
// loop() からいつ呼んでもよい。
//
// 無線は「ペアリング済みの相手がいる」か「ペアリング中」のときだけ有効にする。
// ペアリング情報は NVS に保存され、電源を切っても残る(unpair() で消す)。
#pragma once

#include <Arduino.h>

#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class PocoController {
public:
  // 状態。優先順位は Pairing > Connected > PairingFailed > Disconnected > Unpaired
  enum class State : uint8_t {
    Unpaired,       // ペアリング済みの相手がいない(無線は停止)
    Pairing,        // ペアリング中(コントローラのペアリングボタン待ち → 鍵交換 → 入力待ち)
    PairingFailed,  // 直前のペアリングが失敗した(startPairing() / cancelPairing() で消える)
    Disconnected,   // 相手はいるが入力が届いていない
    Connected,      // 入力が届いている
  };

  // ペアリングを始める(コントローラ側のペアリングボタンも押す)。30 秒で受け付けを終える。
  // すでに相手がいる場合、新しい相手と鍵交換が成立した時点で置き換わる
  void startPairing();

  // ペアリングを中止する
  void cancelPairing();

  // ペアリング情報を消す(相手にも解除を通知する)。無線は停止する
  void unpair();

  // ペアリング済みの相手がいれば true
  bool isPaired() const { return m_hasBond.load(); }

  // 直近 0.5 秒以内に入力が届いていれば true
  bool isConnected() const;

  State state() const;

  // 十字キーの向き(-100〜100)。id 0 = 左右(右が正)、1 = 上下(下が正)。2 以降と未接続時は 0
  int axis(uint8_t id) const;

  // ボタンが押されていれば true。id 0 = A(右) 1 = B(下) 2 = X(上) 3 = Y(左)
  // 12 = 十字キー上 13 = 下 14 = 左 15 = 右。それ以外の id と未接続時は false
  bool button(uint8_t id) const;

  // コントローラの電池が少なければ true(未接続時は false)
  bool batteryLow() const;

private:
  friend class PocoDevices;

  // 保存済みのペアリング情報を読み、受信タスクを起動する。無線はまだ立ち上げない
  bool begin();

  // 無線の状態(タスク内で遷移する)
  enum class RfState : uint8_t {
    Off,      // 無線停止
    Pairing,  // チャネル 1 で常時受信
    Standby,  // 運用チャネルで間欠受信(省電力)
    Driving,  // 運用チャネルで常時受信
  };

  // ペアリングの進行段階
  enum class Phase : uint8_t {
    None,
    CollectBeacon,   // ビーコンを集めて相手を 1 台に絞る
    SendAssign,      // PairAssign(平文)を連続送信
    AwaitPairAck,    // 暗号化した PairAck を待つ
    AwaitCommitAck,  // PairCommit を再送しつつ CommitAck を待つ
    SendSwitch,      // SwitchConfirm を送り、遅延後に運用チャネルへ移る
    AwaitInput,      // 運用チャネルで最初の入力を待つ
  };

  // スケッチから受信タスクへ渡す指示(最新の 1 つだけ保持する)
  enum class Command : uint8_t {
    None,
    StartPairing,
    CancelPairing,
    Unpair,            // スケッチ起点。相手へ解除を通知してから消す
    UnpairFromRemote,  // 相手からの通知起点。通知を送り返さず消すだけ
  };

  // ペアリング情報(NVS に保存する内容)
  struct Bond {
    uint8_t mac[6]   = {};
    uint8_t lmk[16]  = {};
    uint8_t channel  = 0;
  };

  // 最新の入力の写し(m_mux で保護)
  struct Snapshot {
    int64_t  lastRxUs = 0;  // 0 = まだ 1 度も届いていない
    uint16_t buttons  = 0;
    int8_t   axisX    = 0;
    int8_t   axisY    = 0;
    uint8_t  flags    = 0;
  };

  static void taskEntry(void* arg);

  void update();
  void applyCommand();
  void setCommand(Command command) { m_command.store(static_cast<uint8_t>(command)); }

  void doStartPairing();
  void doCancelPairing();
  void doUnpair(bool notifyPeer);
  void clearPairingRuntime();
  void failPairing(const char* reason);

  RfState desiredRfState(int64_t nowUs) const;
  void    enterRfState(RfState state);
  bool    ensureRfStarted();
  void    stopRf();
  void    configurePairingRf();
  void    configureBondedRf(uint8_t channel);
  void    configureWakeWindow(uint16_t windowMs, uint16_t intervalMs, bool powerSave);
  void    setChannel(uint8_t channel);

  // 受信フレームの処理(mac = 送信元、data / len = フレーム本体)
  void drainRecvQueue();
  void processBeacon(const uint8_t* mac, const uint8_t* data, int len);
  void processPairAck(const uint8_t* mac, const uint8_t* data, int len);
  void processCommitAck(const uint8_t* mac, const uint8_t* data, int len);
  void processInput(const uint8_t* mac, const uint8_t* data, int len);
  void processUnpair(const uint8_t* mac, int len);

  void    tickPairing(int64_t nowUs);
  void    beginPairAssign();
  uint8_t selectOperatingChannel() const;
  void    tickPairAssignBurst(int64_t nowUs);
  void    sendPairCommit();
  void    tickPairCommitRetransmit(int64_t nowUs);
  void    tickSwitchConfirm(int64_t nowUs);
  void    parkOnAssignedChannel();
  void    sendUnpairNotice();

  // NVS のペアリング情報(名前空間 "pairing")
  bool loadBond(Bond& out) const;
  bool saveBond(const Bond& bond) const;
  void eraseBond() const;

  Snapshot snapshot() const;
  bool     isConnected(const Snapshot& s, int64_t nowUs) const;

  // --- スケッチ側からも読む(アトミック / m_mux で保護) ---
  mutable portMUX_TYPE  m_mux = portMUX_INITIALIZER_UNLOCKED;
  Snapshot              m_snapshot;
  std::atomic<bool>     m_hasBond{false};
  std::atomic<bool>     m_pairingActive{false};
  std::atomic<bool>     m_pairingFailed{false};
  std::atomic<uint8_t>  m_command{0};

  // --- 受信タスク内だけで触る ---
  TaskHandle_t m_task        = nullptr;
  uint8_t      m_robotMac[6] = {};
  Bond          m_bond;
  Bond          m_pendingBond;

  RfState m_rfState       = RfState::Off;  // 実際に設定した無線の状態
  RfState m_rfTarget      = RfState::Off;  // 直近に要求した無線の状態
  Phase   m_phase         = Phase::None;
  bool    m_rfStarted     = false;         // Wi-Fi(STA)を立ち上げた
  bool    m_espNowStarted = false;         // ESP-NOW を初期化した
  uint8_t m_currentChannel = 0;            // 0 = 未設定

  uint8_t m_candidateMac[6]  = {};  // ビーコンを受けた相手(1 台目)
  int     m_candidateCount   = 0;   // ビーコンを受けた相手の数(2 台以上で失敗)
  int64_t m_firstCandidateUs = 0;
  int64_t m_pairingStartedUs = 0;
  uint32_t m_pendingTxId     = 0;
  int64_t m_lastAssignSentUs = 0;
  int64_t m_lastCommitSentUs = 0;
  int64_t m_lastSwitchSentUs = 0;
  int64_t m_pairCommitSentUs = 0;
  int64_t m_switchAtUs       = 0;
  int     m_assignSentCount  = 0;
  int     m_switchSentCount  = 0;

  // 直近に適用した受信窓の設定(同じ値の再適用を省く)
  uint16_t m_lastWakeWindowMs   = 0;
  uint16_t m_lastWakeIntervalMs = 0;
  bool     m_lastPowerSave      = false;
};
