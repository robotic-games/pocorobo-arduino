// 競技システム ATTRACTS のトランシーバから届く UART フレームの定義と増分パーサ
//
// トランシーバと共有する線上の契約。115200 8N1・3.3 V。ロボット側は受信のみ(送信しない)。
//
// フレーム(ペイロード長 n に対し n + 7 バイト):
//   [0]      開始バイト 0xAE
//   [1]      コマンド(0x00 = 操縦者入力 / 0x01 = 機体状態)
//   [2-3]    ペイロード長 uint16 リトルエンディアン
//   [4]      CRC8(先頭 4 バイトが対象)
//   [5..]    ペイロード(エスケープ無し)
//   [末尾 2] CRC16(先頭から CRC8 を含みペイロード末尾まで)リトルエンディアン
//
// 50 Hz で 1 tick に操縦者入力 → 機体状態が連続して届く。フレーム間のアイドルは保証されないので、
// 「開始バイト + 長さ + CRC」のバイトストリーム同期でパースする。
// プロトコルバージョン欄は無いので、未知コマンド・想定外のペイロード長は黙って捨てる。
//
// ESP 依存の無いヘッダオンリー実装。
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace PocoAttractsProtocol {

// ===== フレーム定数 =====

constexpr uint8_t kStartByte = 0xAE;

constexpr size_t kHeaderSize   = 5;                                       // 開始バイト + コマンド + 長さ(2) + CRC8
constexpr size_t kCrc16Size    = 2;                                       // 末尾の CRC16
constexpr size_t kMaxPayload   = 64;                                      // ペイロード上限
constexpr size_t kMaxFrameSize = kHeaderSize + kMaxPayload + kCrc16Size;  // 71

// コマンド
constexpr uint8_t kCmdDataInput = 0x00;  // 操縦者入力(ペイロード 9 バイト固定)
constexpr uint8_t kCmdDataRobot = 0x01;  // 機体状態(ペイロード 11 バイト固定)

constexpr size_t kDataInputPayloadSize = 9;
constexpr size_t kDataRobotPayloadSize = 11;

// ===== キー ID(線上のビット位置をそのまま通し番号にしたもの) =====
//
//   keyId = byteIndex * 8 + bit
//   byteIndex 0 = マウスボタン、1〜4 = キーボードの各バイト
//
//   0 マウス左  1 マウス右  2 マウス中  3 サイド 1  4 サイド 2  (5〜7 未使用)
//   8 1  9 2  10 3  11 4  12 5  13 Tab  14 Q  15 W
//   16 E  17 R  18 T  19 A  20 S  21 D  22 F  23 G
//   24 H  25 Shift  26 Z  27 X  28 C  29 V  30 B  31 Ctrl
//   32 Alt  33 Space  34 Enter

constexpr int kMaxKeyId = 34;

// 有効なキー ID か(未使用の 5〜7 と範囲外は無効)
constexpr bool isValidKeyId(int keyId) {
  return keyId >= 0 && keyId <= kMaxKeyId && (keyId < 5 || keyId > 7);
}

// ===== デコード結果 =====

// コマンド 0x00 = 操縦者入力
struct Input {
  int16_t  mouseDeltaX = 0;  // 相対移動量(右が正)
  int16_t  mouseDeltaY = 0;  // 相対移動量(上が正)
  uint64_t keyBits     = 0;  // キー ID をビット位置とする押下状態(その瞬間の状態)
};

// コマンド 0x01 = 機体状態
struct Robot {
  uint8_t  role             = 0;  // 0 = Tank 1 = Assault 2 = Standard
  uint8_t  team             = 0;  // 0 = 赤 1 = 青
  uint8_t  bulletSpeedLimit = 0;  // 弾速上限(m/s)
  uint16_t maxHp            = 0;
  uint16_t hp               = 0;
  uint16_t maxHeat          = 0;
  uint16_t heat             = 0;
};

// パーサが 1 バイト食べた結果、フレームが完成したかどうか
enum class FrameKind : uint8_t {
  None,   // まだ完成していない(捨てたバイトも含む)
  Input,  // 操縦者入力が完成した
  Robot,  // 機体状態が完成した
};

// ===== CRC =====

// CRC8(多項式 0x07・初期値 0x00・反射なし・最終 XOR なし)
inline uint8_t crc8(const uint8_t* data, size_t length) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80U) != 0 ? static_cast<uint8_t>((crc << 1) ^ 0x07U) : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

// CRC-16/CCITT-FALSE(多項式 0x1021・初期値 0xFFFF・反射なし・最終 XOR なし)
inline uint16_t crc16(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(static_cast<uint16_t>(data[i]) << 8);
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000U) != 0 ? static_cast<uint16_t>((crc << 1) ^ 0x1021U) : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

// ===== 増分パーサ =====
//
// 1 バイトずつ食わせる。開始バイト探索 → ヘッダ CRC8 → 長さ → ペイロード → CRC16 の順に検査し、
// 不一致なら先頭 1 バイトだけ捨てて次の候補バイトから再同期する。
// 未知コマンド・想定外のペイロード長はフレームごと黙って捨て、パースは続ける。
class FrameParser {
public:
  // 1 バイト食わせる。フレームが完成したときだけ Input / Robot を返す
  FrameKind feed(uint8_t byte) {
    if (m_length >= kMaxFrameSize) {
      // scan() の不変条件(保持するのは未完成フレームの前半だけ)から到達しない想定。
      // 万一到達しても先頭を捨てて前進させる
      dropFront(1);
    }
    m_buffer[m_length++] = byte;
    return scan();
  }

  // 最後に完成した操縦者入力 / 機体状態
  const Input& lastInput() const { return m_lastInput; }
  const Robot& lastRobot() const { return m_lastRobot; }

private:
  // 先頭 count バイトを捨てて詰める
  void dropFront(size_t count) {
    for (size_t i = count; i < m_length; ++i) {
      m_buffer[i - count] = m_buffer[i];
    }
    m_length -= count;
  }

  uint16_t readU16(size_t offset) const {
    return static_cast<uint16_t>(m_buffer[offset] | (static_cast<uint16_t>(m_buffer[offset + 1]) << 8));
  }

  int16_t readI16(size_t offset) const { return static_cast<int16_t>(readU16(offset)); }

  // バッファ先頭からフレームを 1 つ切り出す。切り出せない間はバイトを捨てながら前進する
  FrameKind scan() {
    while (m_length > 0) {
      if (m_buffer[0] != kStartByte) {
        dropFront(1);
        continue;
      }
      if (m_length < kHeaderSize) {
        return FrameKind::None;  // ヘッダ待ち
      }
      if (crc8(m_buffer, kHeaderSize - 1) != m_buffer[kHeaderSize - 1]) {
        dropFront(1);  // 開始バイトに見えただけのデータ。次の候補から再同期する
        continue;
      }

      const size_t payloadSize = readU16(2);
      if (payloadSize > kMaxPayload) {
        dropFront(1);  // 長さが信用できないのでフレーム全体を読み飛ばさない
        continue;
      }

      const size_t frameSize = kHeaderSize + payloadSize + kCrc16Size;
      if (m_length < frameSize) {
        return FrameKind::None;  // ペイロード / CRC16 待ち
      }

      const uint16_t expected = crc16(m_buffer, kHeaderSize + payloadSize);
      if (expected != readU16(kHeaderSize + payloadSize)) {
        dropFront(1);
        continue;
      }

      const FrameKind kind = decode(m_buffer[1], payloadSize);
      dropFront(frameSize);
      if (kind != FrameKind::None) {
        return kind;
      }
      // 未知コマンド・想定外長は黙って捨て、続きのフレームを探す
    }
    return FrameKind::None;
  }

  // CRC 検証済みフレームのペイロードを解釈する。未知コマンド・想定外長は None
  FrameKind decode(uint8_t command, size_t payloadSize) {
    const size_t payload = kHeaderSize;  // ペイロード先頭オフセット

    if (command == kCmdDataInput && payloadSize == kDataInputPayloadSize) {
      m_lastInput.mouseDeltaX = readI16(payload + 0);
      m_lastInput.mouseDeltaY = readI16(payload + 2);
      // keyId = byteIndex * 8 + bit。線上のバイト順をそのままビット位置へ並べる
      uint64_t bits = 0;
      for (size_t i = 0; i < 5; ++i) {
        bits |= static_cast<uint64_t>(m_buffer[payload + 4 + i]) << (i * 8);
      }
      m_lastInput.keyBits = bits;
      return FrameKind::Input;
    }

    if (command == kCmdDataRobot && payloadSize == kDataRobotPayloadSize) {
      m_lastRobot.role             = m_buffer[payload + 0];
      m_lastRobot.team             = m_buffer[payload + 1];
      m_lastRobot.bulletSpeedLimit = m_buffer[payload + 2];
      m_lastRobot.maxHp            = readU16(payload + 3);
      m_lastRobot.hp               = readU16(payload + 5);
      m_lastRobot.maxHeat          = readU16(payload + 7);
      m_lastRobot.heat             = readU16(payload + 9);
      return FrameKind::Robot;
    }

    return FrameKind::None;
  }

  uint8_t m_buffer[kMaxFrameSize] = {};
  size_t  m_length                = 0;

  Input m_lastInput;
  Robot m_lastRobot;
};

}  // namespace PocoAttractsProtocol
