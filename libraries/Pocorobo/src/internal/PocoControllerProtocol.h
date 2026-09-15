// 専用コントローラとの無線(ESP-NOW)フレーム定義
//
// コントローラ本体と共有する電波上の契約。ここにある値(マジック・バージョン・各フレームの
// サイズとフィールド順・鍵・チャネル候補・時間定数)を変えるとコントローラと通信できなくなる。
//
//  - 全フレームは packed・固定サイズ・リトルエンディアン(ESP32 同士の通信)
//  - 先頭の FrameHeader{magic, version, frameType, txId} で種別を判定する
//  - ペアリングはチャネル 1 で鍵交換 → 割り当てた運用チャネル({1, 6, 11})へ一緒に移動する
//  - PairAssign だけ平文(鍵を運ぶため)。それ以降は暗号化ユニキャスト限定
//  - 定常通信はコントローラ → ロボットの一方向。解除通知(Unpair)だけ双方向
#pragma once

#include <stdint.h>
#include <string.h>

namespace PocoControllerProtocol {

// プロトコルバージョン(フレーム形式を変えたときに上げる)
constexpr uint8_t kProtocolVersion = 1;

// 全フレーム先頭の識別マジック('P' 'O' をリトルエンディアンで読んだ値)
constexpr uint16_t kMagic = 0x4F50;

// 機器種別(ビーコンの足切り用)
constexpr uint8_t kDeviceTypeController = 0x01;

// ペアリング探索チャネル(固定)
constexpr uint8_t kPairingChannel = 1;

// 運用チャネル候補(ペアリング時にロボットが 1 つ選んでコントローラに伝える)
constexpr uint8_t kOperatingChannels[]   = {1, 6, 11};
constexpr int     kOperatingChannelCount = 3;

// ペアごとの暗号鍵(LMK)の長さ
constexpr int kLmkLen = 16;

// 全機器共通の一次鍵(PMK)。LMK の暗号化に使う
constexpr uint8_t kPrimaryMasterKey[16] = {
  0x70, 0x6f, 0x63, 0x6f, 0x72, 0x6f, 0x62, 0x6f, 0x2d, 0x65, 0x73, 0x70, 0x6e, 0x6f, 0x77, 0x31};

// --- 時間・再送の契約値(コントローラ側と一致させる) ---

// ビーコン送信周期(約 5 Hz)
constexpr uint32_t kBeaconIntervalMs = 200;

// ペアリング受付タイムアウト
constexpr uint32_t kPairingTimeoutMs = 30000;

// コントローラ側のペアリング要求ラッチの寿命
constexpr uint64_t kPairLatchTtlUs = 20000000;

// PairAssign(平文)の連続送信回数と間隔(取りこぼし吸収。バースト後は再送しない)
constexpr int      kPairAssignBurstCount      = 8;
constexpr uint32_t kPairAssignBurstIntervalMs = 20;

// PairAck の再送間隔(コントローラが PairCommit を受け取るまで短周期で再送する)
constexpr uint32_t kPairAckRetransmitIntervalMs = 30;

// SwitchConfirm の再送回数と間隔(ロボットが短い窓で複数回送る)
constexpr int      kSwitchConfirmRetransmitCount      = 6;
constexpr uint32_t kSwitchConfirmRetransmitIntervalMs = 20;

// チャネル移動の相対遅延(再送窓を含む長め)
constexpr uint16_t kSwitchDelayMs = 300;

// PairCommit 後のフォールバック: CommitAck が来なくても一定時間後に運用チャネルへ移る
constexpr uint32_t kCommitParkFallbackMs = 1000;

// 定常入力の送信周期(100 Hz)
constexpr uint32_t kInputIntervalMs = 10;

// 解除通知のバースト(暗号化ユニキャスト・ベストエフォート)
// コントローラ → ロボット: ロボットは省電力で受信窓があるため間隔を空ける
constexpr int      kUnpairBurstCount      = 5;
constexpr uint32_t kUnpairBurstIntervalMs = 20;
// ロボット → コントローラ: コントローラは常時受信なので間隔なしで連続送信
constexpr int kUnpairRobotBurstCount = 3;

// ロボット側フェイルセーフ: この時間入力が届かなければ未接続(入力は中立)とみなす
constexpr uint64_t kInputTimeoutUs = 500000;

// ロボット側状態遷移: 入力がこの時間届かなければ運転中 → 待機へ戻す
constexpr uint64_t kDrivingIdleTimeoutUs = 2000000;

// 待機中(電池駆動)の受信窓と周期(省電力)
constexpr uint16_t kStandbyWakeWindowMs   = 10;
constexpr uint32_t kStandbyWakeIntervalMs = 100;

// 入力パケットの flags ビット
constexpr uint8_t kFlagBatteryLow = 0x01;  // bit0: コントローラの電池低下

// ビーコンの flags ビット
constexpr uint8_t kBeaconFlagPairingRequest = 0x01;  // bit0: ペアリングボタンが押された

// --- フレーム種別 ---

enum class FrameType : uint8_t {
  Beacon        = 1,  // コントローラ → ブロードキャスト(ch1): ペアリング探索
  PairAssign    = 2,  // ロボット → コントローラ(ch1, 平文): ロボット MAC + LMK + 運用チャネル
  PairAck       = 3,  // コントローラ → ロボット(ch1, 暗号): LMK 保持の証明
  PairCommit    = 4,  // ロボット → コントローラ(ch1, 暗号): 運用チャネル確定
  CommitAck     = 5,  // コントローラ → ロボット(ch1, 暗号)
  SwitchConfirm = 6,  // ロボット → コントローラ(ch1, 暗号): 移動までの遅延
  Input         = 7,  // コントローラ → ロボット(運用ch, 暗号): 定常入力(一方向)
  Unpair        = 8,  // 双方向(運用ch, 暗号): ペアリング解除の通知
};

#pragma pack(push, 1)

// 全フレーム共通ヘッダ(8 バイト)
struct FrameHeader {
  uint16_t magic;      // = kMagic
  uint8_t  version;    // = kProtocolVersion
  uint8_t  frameType;  // FrameType
  uint32_t txId;       // ペアリングのトランザクション ID(Beacon / Input / Unpair は 0)
};
static_assert(sizeof(FrameHeader) == 8, "FrameHeader は 8 バイト固定");

// 定常入力のペイロード(8 バイト)
struct InputPacket {
  uint8_t  version;   // = kProtocolVersion
  uint8_t  seq;       // 連番(取りこぼし・順序の検知用)
  uint16_t buttons;   // bitN = ボタン N が押されている(N = 0..15)
  int8_t   axisX;     // 左右: -127..127(右が正)
  int8_t   axisY;     // 上下: -127..127(下が正)
  uint8_t  flags;     // kFlag* のビット和
  uint8_t  reserved;  // 0
};
static_assert(sizeof(InputPacket) == 8, "InputPacket は 8 バイト固定");

// ビーコン(コントローラ → ブロードキャスト, ch1)
struct BeaconFrame {
  FrameHeader header;      // frameType = Beacon, txId = 0
  uint8_t     deviceType;  // = kDeviceTypeController
  uint8_t     flags;       // kBeaconFlag* のビット和
};
static_assert(sizeof(BeaconFrame) == 10, "BeaconFrame は 10 バイト固定");

// PairAssign(ロボット → コントローラ, ch1, 平文)
struct PairAssignFrame {
  FrameHeader header;           // frameType = PairAssign, txId
  uint8_t     robotMac[6];      // ロボットの STA MAC
  uint8_t     lmk[kLmkLen];     // ペア鍵
  uint8_t     assignedChannel;  // 運用チャネル({1, 6, 11})
  uint8_t     reserved;         // 0
};
static_assert(sizeof(PairAssignFrame) == 8 + 6 + 16 + 1 + 1, "PairAssignFrame のサイズ不一致");

// PairAck(コントローラ → ロボット, ch1, 暗号)
struct PairAckFrame {
  FrameHeader header;  // frameType = PairAck, txId
};
static_assert(sizeof(PairAckFrame) == 8, "PairAckFrame は 8 バイト固定");

// PairCommit(ロボット → コントローラ, ch1, 暗号)
struct PairCommitFrame {
  FrameHeader header;           // frameType = PairCommit, txId
  uint8_t     assignedChannel;  // 確認用(PairAssign と同じ値)
  uint8_t     reserved;         // 0
};
static_assert(sizeof(PairCommitFrame) == 10, "PairCommitFrame は 10 バイト固定");

// CommitAck(コントローラ → ロボット, ch1, 暗号)
struct CommitAckFrame {
  FrameHeader header;  // frameType = CommitAck, txId
};
static_assert(sizeof(CommitAckFrame) == 8, "CommitAckFrame は 8 バイト固定");

// SwitchConfirm(ロボット → コントローラ, ch1, 暗号)
struct SwitchConfirmFrame {
  FrameHeader header;         // frameType = SwitchConfirm, txId
  uint16_t    switchDelayMs;  // 受信時点からの相対遅延
  uint16_t    reserved;       // 0
};
static_assert(sizeof(SwitchConfirmFrame) == 12, "SwitchConfirmFrame は 12 バイト固定");

// 定常入力(コントローラ → ロボット, 運用ch, 暗号, 一方向)
struct InputFrame {
  FrameHeader header;  // frameType = Input, txId = 0
  InputPacket input;   // 8 バイト
};
static_assert(sizeof(InputFrame) == 16, "InputFrame は 16 バイト固定");

// 解除通知(双方向, 運用ch, 暗号, ベストエフォート)
struct UnpairFrame {
  FrameHeader header;  // frameType = Unpair, txId = 0
};
static_assert(sizeof(UnpairFrame) == 8, "UnpairFrame は 8 バイト固定");

#pragma pack(pop)

// 受信コールバック用の軽い判定(マジック・バージョン・最小長)。重い処理はキューの先で行う
inline bool isValidHeader(const uint8_t* data, int len) {
  if (data == nullptr || len < static_cast<int>(sizeof(FrameHeader))) {
    return false;
  }
  FrameHeader header;
  memcpy(&header, data, sizeof(header));  // アライメントのためコピーして読む
  return header.magic == kMagic && header.version == kProtocolVersion;
}

}  // namespace PocoControllerProtocol
