// USB ゲームパッド(Type-A コネクタ)の入力
//
// begin() を呼ぶと、ライブラリ内の専用タスクが Type-C の給電(パソコンの接続)を 0.5 秒ごとに見て、
// USB のつなぎ先を自動で切り替える。切り替えの規則は標準プログラムと同じ。
//   パソコンあり: Type-C をつなぎ、書き込み口とシリアルモニタ(Serial)が使える
//   パソコンなし: Type-A に給電してゲームパッドを待つ。この間 Serial は使えない(出力は捨てられる)
// ゲームパッドで動かしている本体に書き込むときは、パソコンをつないでポートが出るまで(数秒)待ってから書く。
//
// begin() を呼ばなければ USB は Type-C 固定のまま(今までどおり)。
// axis() / button() は最新の入力の写しを読むだけで、loop() からいつ呼んでもよい。
// 対応するゲームパッド: 一般的な HID ゲームパッド(JC-U3912TBK など)、Xbox 互換、HORIPAD STEAM
#pragma once

#include <Arduino.h>

#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "internal/PocoGamepadState.h"
#include "usb/usb_host.h"

class PocoController;

class PocoGamepad {
public:
  // USB の自動切り替えを始める。Poco.begin() の後に呼ぶ。2 回目以降は何もしない
  bool begin();

  // ゲームパッドがつながって入力を受けていれば true
  bool isConnected() const { return m_deviceConnected.load(); }

  // Type-C にパソコン(給電)がつながっていれば true。begin() 前は false
  bool isPcConnected() const { return m_pcConnected.load(); }

  // スティックの向き(-100〜100)。id 0 = 左スティック左右(右が正) 1 = 上下(下が正)
  // 2 = 右スティック左右 3 = 上下。それ以外の id と未接続時は 0(十字キーは button(12〜15) で読む)
  int axis(uint8_t id) const;

  // ボタンが押されていれば true。番号は専用コントローラと共通:
  // 0 = A(下) 1 = B(右) 2 = X(左) 3 = Y(上) 4 = LB 5 = RB 6 = LT 7 = RT 8 = Back 9 = Start
  // 10 = 左スティック押し込み 11 = 右スティック押し込み 12 = 十字キー上 13 = 下 14 = 左 15 = 右 16 = Home
  // それ以外の id と未接続時は false
  bool button(uint8_t id) const;

private:
  friend class PocoDevices;

  // USB のつなぎ先
  enum class Mode : uint8_t {
    Pc,       // Type-C(書き込み口・Serial)
    Gamepad,  // Type-A(ゲームパッド)
  };

  // USB の状態が変わるたびに専用コントローラ(無線)へ知らせる相手。Poco.begin() がつなぐ
  void linkController(PocoController* controller) { m_controller = controller; }

  static void monitorTaskEntry(void* arg);
  void        monitorTask();

  bool readPcConnected() const;
  void switchToPc();
  void switchToGamepad();
  void restoreSerialJtag();
  void publishUsbState();

  // ---- USB Host(ゲームパッド側) ----
  void        hostEnable();
  void        hostDisable();
  void        initializeHostDriver();
  void        shutdownHostDriver();
  static void waitForUsbPhyRelease();

  static void clientEventCallback(const usb_host_client_event_msg_t* eventMsg, void* arg);
  static void hostTask(void* arg);
  static void clientTask(void* arg);
  static void transferCallback(usb_transfer_t* transfer);
  static void controlTransferCallback(usb_transfer_t* transfer);
  static void outTransferCallback(usb_transfer_t* transfer);

  void handleNewDevice(uint8_t address);
  void cleanupDevice();
  bool setupGamepad(uint8_t interfaceNum);
  void releaseGamepadInterface();
  bool isGamepadDevice(uint8_t& interfaceNum);
  // claim 対象インターフェース(代替設定 0)配下の Interrupt-IN エンドポイント。無ければ 0
  uint8_t findInEndpoint(uint8_t interfaceNum, uint16_t& maxPacketSize);
  // Xbox 互換インターフェース(class 0xFF)配下の Interrupt-OUT エンドポイント。無ければ 0
  uint8_t findXboxOutEndpoint(uint8_t interfaceNum);
  // XInput 系は LED 設定を送らないと入力を流さない個体があるため送る
  void sendXboxLedCommand(uint8_t interfaceNum);

  void             storeState(const PocoGamepadState& state);
  PocoGamepadState loadState() const;

  // コールバックから辿るための唯一のインスタンス(Poco.gamepad)
  static inline PocoGamepad* s_instance = nullptr;

  // --- スケッチ側からも読む(アトミック / m_mux で保護) ---
  mutable portMUX_TYPE m_mux = portMUX_INITIALIZER_UNLOCKED;
  PocoGamepadState     m_state{};
  std::atomic<bool>    m_pcConnected{false};
  std::atomic<bool>    m_deviceConnected{false};

  // --- 監視タスク・USB タスク内だけで触る ---
  PocoController*          m_controller = nullptr;
  bool                     m_began      = false;
  std::atomic<Mode>        m_mode{Mode::Pc};
  bool                     m_hostEnabled{false};
  TaskHandle_t             m_hostTaskHandle{};
  TaskHandle_t             m_clientTaskHandle{};
  usb_host_client_handle_t m_clientHandle{};
  volatile bool            m_shutdownRequested{false};
  usb_device_handle_t      m_deviceHandle{};
  usb_transfer_t*          m_dataTransfer{};
  uint8_t                  m_claimedInterface{0};
};
