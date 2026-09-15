// ボード固有の初期化（スケッチの setup() より前に呼ばれる）
#include "HWCDC.h"
#include "hal/usb_serial_jtag_ll.h"

extern "C" void initVariant() {
  // 内蔵 USB PHY を USB-Serial/JTAG 側に接続する。
  // 直前に動いていたプログラムが USB を別の用途（ネットワーク接続など）に切り替えたままでも、
  // その設定は再起動を越えて残るため、ここで明示的に戻して書き込みとシリアルモニタを使えるようにする
  usb_serial_jtag_ll_phy_enable_external(false);
  usb_serial_jtag_ll_phy_enable_pad(true);

  // Serial（USB CDC）の送信を待たせない。
  // USB をつないだままシリアルモニタを閉じていると、既定では Serial.println() 1 行で最大 4 秒
  // （write() 1 回あたり最大 2 秒 × 2 回）ブロックし、ロボットの動きが間延びする。
  // 受け取る相手がいないときは出力を捨て、ロボットの動きを止めない
  HWCDCSerial.setTxTimeoutMs(0);
}
