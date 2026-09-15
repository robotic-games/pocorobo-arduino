// ボード固有の初期化（スケッチの setup() より前に呼ばれる）
#include "hal/usb_serial_jtag_ll.h"

extern "C" void initVariant() {
  // 内蔵 USB PHY を USB-Serial/JTAG 側に接続する。
  // 直前に動いていたプログラムが USB を別の用途（ネットワーク接続など）に切り替えたままでも、
  // その設定は再起動を越えて残るため、ここで明示的に戻して書き込みとシリアルモニタを使えるようにする
  usb_serial_jtag_ll_phy_enable_external(false);
  usb_serial_jtag_ll_phy_enable_pad(true);
}
