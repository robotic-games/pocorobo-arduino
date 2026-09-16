// 競技システム ATTRACTS のトランシーバからの受信(UART コネクタ)
//
// トランシーバの信号線を UART コネクタの RX につなぎ、setup() で begin() を呼ぶと、
// ライブラリ内の専用タスクが受信と解析を続ける。スケッチ側で定期的に呼ぶものはない。
// key() や hp() などは最新の受信内容の写しを読むだけで、loop() からいつ呼んでもよい。
//
// 受信専用(TX は使わない)。begin() 以後、UART コネクタと Serial1 はスケッチから使えない。
#pragma once

#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "internal/PocoAttractsProtocol.h"

class PocoAttracts {
public:
  // 操縦者のキー(番号はトランシーバとの契約なので変えない)
  enum class Key : uint8_t {
    MouseLeft   = 0,
    MouseRight  = 1,
    MouseMiddle = 2,
    MouseSide1  = 3,
    MouseSide2  = 4,
    Num1        = 8,
    Num2        = 9,
    Num3        = 10,
    Num4        = 11,
    Num5        = 12,
    Tab         = 13,
    Q           = 14,
    W           = 15,
    E           = 16,
    R           = 17,
    T           = 18,
    A           = 19,
    S           = 20,
    D           = 21,
    F           = 22,
    G           = 23,
    H           = 24,
    Shift       = 25,
    Z           = 26,
    X           = 27,
    C           = 28,
    V           = 29,
    B           = 30,
    Ctrl        = 31,
    Alt         = 32,
    Space       = 33,
    Enter       = 34,
  };

  // 受信を始める(setup() で Poco.begin() の後に 1 回)。失敗すると false
  bool begin();

  // 直近 0.2 秒以内に操縦者入力が届いていれば true
  bool isConnected() const;

  // キーが押されていれば true(未接続時は false)
  bool key(Key key) const;

  // マウスの移動量。前回この関数で読んでからの差分で、読むと 0 に戻る。X は右が正、Y は上が正
  int mouseDeltaX();
  int mouseDeltaY();

  // マウスの累積移動量(begin() からの合計)。X は右が正、Y は上が正
  int mouseTotalX() const;
  int mouseTotalY() const;

  // 機体の状態(未接続でも最後に届いた値を返す。1 度も届いていなければ 0)
  int hp() const;
  int maxHp() const;
  int heat() const;
  int maxHeat() const;
  int team() const;              // 0 = 赤、1 = 青
  int role() const;              // 0 = Tank、1 = Assault、2 = Standard
  int bulletSpeedLimit() const;  // 弾速上限(m/s)

private:
  // 最新の受信内容の写し(m_mux で保護)
  struct Snapshot {
    int64_t                     lastInputUs = 0;  // 操縦者入力を最後に受けた時刻。0 = まだ 1 度も届いていない
    uint64_t                    keyBits     = 0;  // 最後に受けた押下状態
    int32_t                     accumX      = 0;  // マウス X の累積(受信ごとに加算。桁あふれはそのまま一周)
    int32_t                     accumY      = 0;
    PocoAttractsProtocol::Robot robot;             // 最後に受けた機体状態
  };

  static void taskEntry(void* arg);
  void        receiveLoop();
  Snapshot    snapshot() const;

  // --- スケッチ側からも読む(m_mux で保護) ---
  mutable portMUX_TYPE m_mux = portMUX_INITIALIZER_UNLOCKED;
  Snapshot             m_snapshot;

  // --- スケッチ側だけで触る(mouseDelta*() の基準) ---
  int32_t m_lastReadX = 0;
  int32_t m_lastReadY = 0;

  // --- 受信タスク内だけで触る ---
  TaskHandle_t                      m_task = nullptr;
  PocoAttractsProtocol::FrameParser m_parser;
};
