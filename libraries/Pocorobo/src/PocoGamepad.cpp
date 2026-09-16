// USB ゲームパッド(PocoGamepad)の実装
//
// ESP32-S3 の USB は「書き込み口(USB-Serial/JTAG)」と「ゲームパッドを読む USB Host」で 1 つの PHY
// (信号を出す回路)を取り合う。Host を立ち上げると PHY は Host 側へ移り、書き込み口はパソコンから
// 見えなくなる。そこで Type-C の給電を見張り、パソコンが来たら Host を止めて PHY を書き込み口へ戻す。
#include "internal/PocoGamepad.h"

#include <cmath>
#include <cstring>

#include "internal/PocoController.h"
#include "internal/PocoGamepadDecoder.h"

extern "C" {
#include "esp_private/periph_ctrl.h"
#include "esp_private/usb_phy.h"
#include "hal/usb_serial_jtag_ll.h"
#include "soc/periph_defs.h"
}

namespace {
// 給電の監視タスク
constexpr uint32_t    kMonitorStackSize  = 4096;
constexpr UBaseType_t kMonitorPriority   = 5;
constexpr uint32_t    kMonitorIntervalMs = 500;

// HID の SET_IDLE 要求(idle 時間 0 = 入力が変わったときだけ報告させる)
constexpr uint8_t kHidRequestSetIdle = 0x0A;
}  // namespace

// ---- 開始と監視 ------------------------------------------------------------

bool PocoGamepad::begin() {
  if (m_began) {
    return true;
  }
  m_began    = true;
  s_instance = this;

  pinMode(PIN_USB_SELECT, OUTPUT);

  // 起動時に 1 回判定してから、そのつなぎ先で始める。書き込み口は起動時からつながっているので、
  // パソコンがあるときは何もしない
  if (readPcConnected()) {
    digitalWrite(PIN_USB_SELECT, LOW);
    m_mode.store(Mode::Pc);
    m_pcConnected.store(true);
  } else {
    switchToGamepad();
  }
  publishUsbState();

  if (xTaskCreate(monitorTaskEntry, "usb_monitor", kMonitorStackSize, this, kMonitorPriority, nullptr) != pdPASS) {
    log_e("gamepad: 監視タスクを作れません");
    return false;
  }
  return true;
}

void PocoGamepad::monitorTaskEntry(void* arg) {
  static_cast<PocoGamepad*>(arg)->monitorTask();
}

void PocoGamepad::monitorTask() {
  while (true) {
    const bool pc = readPcConnected();
    if (pc && m_mode.load() != Mode::Pc) {
      switchToPc();
    } else if (!pc && m_mode.load() != Mode::Gamepad) {
      switchToGamepad();
    }
    publishUsbState();
    vTaskDelay(pdMS_TO_TICKS(kMonitorIntervalMs));
  }
}

// Type-C の給電(VBUS の分圧)がしきい値を超えていればパソコンあり
bool PocoGamepad::readPcConnected() const {
  return analogReadMilliVolts(PIN_USB_SENSE) > POCOROBO_USB_SENSE_THRESHOLD_MV;
}

// パソコン側へ: Type-C をつなぐ → Host を止める → PHY を書き込み口へ戻す
void PocoGamepad::switchToPc() {
  digitalWrite(PIN_USB_SELECT, LOW);
  hostDisable();
  restoreSerialJtag();
  m_mode.store(Mode::Pc);
  m_pcConnected.store(true);
}

// ゲームパッド側へ: Type-A に給電 → Host を立ち上げる(PHY は Host が取る)
void PocoGamepad::switchToGamepad() {
  digitalWrite(PIN_USB_SELECT, HIGH);
  hostEnable();
  m_mode.store(Mode::Gamepad);
  m_pcConnected.store(false);
}

// 内蔵 USB PHY を書き込み口(USB-Serial/JTAG)へ戻す。起動時の初期化と同じ手順
void PocoGamepad::restoreSerialJtag() {
  waitForUsbPhyRelease();
  usb_serial_jtag_ll_phy_enable_external(false);
  usb_serial_jtag_ll_phy_enable_pad(true);
}

void PocoGamepad::publishUsbState() {
  if (m_controller != nullptr) {
    m_controller->setUsbState(m_pcConnected.load(), m_deviceConnected.load());
  }
}

// ---- 入力の読み出し ----------------------------------------------------------

int PocoGamepad::axis(uint8_t id) const {
  if (id >= PocoGamepadState::kAxisCount || !isConnected()) {
    return 0;
  }
  const float v = loadState().axes[id];
  const int   scaled = static_cast<int>(std::lround(v * 100.0F));
  return (scaled > 100) ? 100 : (scaled < -100) ? -100 : scaled;
}

bool PocoGamepad::button(uint8_t id) const {
  if (id >= PocoGamepadState::kButtonCount || !isConnected()) {
    return false;
  }
  return loadState().buttons[id].pressed;
}

void PocoGamepad::storeState(const PocoGamepadState& state) {
  taskENTER_CRITICAL(&m_mux);
  m_state = state;
  taskEXIT_CRITICAL(&m_mux);
}

PocoGamepadState PocoGamepad::loadState() const {
  taskENTER_CRITICAL(&m_mux);
  const PocoGamepadState copy = m_state;
  taskEXIT_CRITICAL(&m_mux);
  return copy;
}

// ---- USB Host の立ち上げと停止 -----------------------------------------------

void PocoGamepad::hostEnable() {
  if (m_hostEnabled) {
    return;
  }
  waitForUsbPhyRelease();
  periph_module_reset(PERIPH_USB_MODULE);
  vTaskDelay(pdMS_TO_TICKS(300));
  initializeHostDriver();
  vTaskDelay(pdMS_TO_TICKS(100));

  // Host を立ち上げる前からつながっていた機器を拾う
  uint8_t devAddrList[10];
  int     numDevs;
  if (usb_host_device_addr_list_fill(10, devAddrList, &numDevs) == ESP_OK && numDevs > 0) {
    handleNewDevice(devAddrList[0]);
  }
  m_hostEnabled = true;
}

void PocoGamepad::hostDisable() {
  if (!m_hostEnabled) {
    return;
  }
  m_shutdownRequested = true;
  shutdownHostDriver();
  m_hostEnabled = false;
}

void PocoGamepad::initializeHostDriver() {
  m_shutdownRequested = false;
  usb_host_config_t hostConfig = {
    .skip_phy_setup      = false,
    .root_port_unpowered = false,
    .intr_flags          = ESP_INTR_FLAG_LEVEL1,
    .enum_filter_cb      = nullptr,
  };
  if (usb_host_install(&hostConfig) != ESP_OK) {
    abort();
  }
  usb_host_client_config_t clientConfig = {
    .is_synchronous    = false,
    .max_num_event_msg = 5,
    .async             = {
      .client_event_callback = clientEventCallback,
      .callback_arg          = this,
    },
  };
  if (usb_host_client_register(&clientConfig, &m_clientHandle) != ESP_OK) {
    usb_host_uninstall();
    abort();
  }
  xTaskCreate(hostTask, "usb_host", 8192, this, 10, &m_hostTaskHandle);
  xTaskCreate(clientTask, "usb_client", 8192, this, 5, &m_clientTaskHandle);
}

void PocoGamepad::shutdownHostDriver() {
  m_shutdownRequested = true;
  if (m_clientHandle) {
    usb_host_client_unblock(m_clientHandle);
  }
  if (m_clientTaskHandle) {
    for (int i = 0; i < 100 && m_clientTaskHandle; i++) {
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
  periph_module_disable(PERIPH_USB_MODULE);
  vTaskDelay(pdMS_TO_TICKS(100));
  cleanupDevice();
  if (m_clientHandle) {
    usb_host_client_deregister(m_clientHandle);
    m_clientHandle = nullptr;
  }
  usb_host_device_free_all();
  for (int i = 0; i < 20; i++) {
    uint32_t eventFlags = 0;
    usb_host_lib_handle_events(pdMS_TO_TICKS(100), &eventFlags);
    if (eventFlags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
      break;
    }
  }
  if (m_hostTaskHandle) {
    vTaskDelete(m_hostTaskHandle);
    m_hostTaskHandle = nullptr;
  }
  usb_host_uninstall();
}

void PocoGamepad::hostTask(void* arg) {
  auto* self = static_cast<PocoGamepad*>(arg);
  while ((self != nullptr) && !self->m_shutdownRequested) {
    usb_host_lib_handle_events(pdMS_TO_TICKS(10), nullptr);
  }
  if (self != nullptr) {
    self->m_hostTaskHandle = nullptr;
  }
  vTaskDelete(nullptr);
}

void PocoGamepad::clientTask(void* arg) {
  auto* self = static_cast<PocoGamepad*>(arg);
  while (self && self->m_clientHandle && !self->m_shutdownRequested) {
    usb_host_client_handle_events(self->m_clientHandle, 100);
  }
  if (self != nullptr) {
    self->m_clientTaskHandle = nullptr;
  }
  vTaskDelete(nullptr);
}

// 前の持ち主が PHY を手放すまで待つ(最大 5 秒)。手放さなければ停止する
void PocoGamepad::waitForUsbPhyRelease() {
  for (int i = 0; i < 50; i++) {
    usb_phy_status_t status;
    esp_err_t        ret = usb_phy_get_phy_status(USB_PHY_TARGET_INT, &status);
    if ((ret == ESP_OK && status == USB_PHY_STATUS_FREE) || ret == ESP_ERR_INVALID_STATE) {
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  abort();
}

// ---- 機器の検出と設定 ---------------------------------------------------------

void PocoGamepad::clientEventCallback(const usb_host_client_event_msg_t* eventMsg, void* arg) {
  auto* self = static_cast<PocoGamepad*>(arg);
  if (self == nullptr) {
    return;
  }
  if (eventMsg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    self->handleNewDevice(eventMsg->new_dev.address);
  } else if (eventMsg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    self->cleanupDevice();
  }
}

void PocoGamepad::handleNewDevice(uint8_t address) {
  if (!m_clientHandle) {
    return;
  }
  if (usb_host_device_open(m_clientHandle, address, &m_deviceHandle) != ESP_OK) {
    return;
  }
  uint8_t gamepadInterface = 0;
  if (isGamepadDevice(gamepadInterface) && setupGamepad(gamepadInterface)) {
    m_deviceConnected.store(true);
    publishUsbState();
    return;
  }
  usb_host_device_close(m_clientHandle, m_deviceHandle);
  m_deviceHandle = nullptr;
}

void PocoGamepad::cleanupDevice() {
  if (!m_clientHandle) {
    return;
  }
  m_deviceConnected.store(false);
  storeState(PocoGamepadState{});
  if (m_dataTransfer) {
    if (m_deviceHandle && m_dataTransfer->bEndpointAddress != 0) {
      usb_host_endpoint_flush(m_deviceHandle, m_dataTransfer->bEndpointAddress);
    }
    usb_host_transfer_free(m_dataTransfer);
    m_dataTransfer = nullptr;
  }
  if (m_deviceHandle) {
    usb_host_interface_release(m_clientHandle, m_deviceHandle, m_claimedInterface);
    usb_host_device_close(m_clientHandle, m_deviceHandle);
    m_deviceHandle = nullptr;
  }
  publishUsbState();
}

bool PocoGamepad::setupGamepad(uint8_t interfaceNum) {
  if (usb_host_interface_claim(m_clientHandle, m_deviceHandle, interfaceNum, 0) != ESP_OK) {
    return false;
  }
  m_claimedInterface = interfaceNum;

  usb_transfer_t* ctrlTransfer;
  if (usb_host_transfer_alloc(256, 0, &ctrlTransfer) == ESP_OK) {
    auto* setup                    = reinterpret_cast<usb_setup_packet_t*>(ctrlTransfer->data_buffer);
    setup->bmRequestType           = USB_BM_REQUEST_TYPE_TYPE_CLASS | USB_BM_REQUEST_TYPE_RECIP_INTERFACE | USB_BM_REQUEST_TYPE_DIR_OUT;
    setup->bRequest                = kHidRequestSetIdle;
    setup->wValue                  = 0;
    setup->wIndex                  = interfaceNum;
    setup->wLength                 = 0;
    ctrlTransfer->num_bytes        = sizeof(usb_setup_packet_t);
    ctrlTransfer->device_handle    = m_deviceHandle;
    ctrlTransfer->bEndpointAddress = 0;
    ctrlTransfer->callback         = controlTransferCallback;
    ctrlTransfer->context          = nullptr;
    if (usb_host_transfer_submit_control(m_clientHandle, ctrlTransfer) != ESP_OK) {
      usb_host_transfer_free(ctrlTransfer);
    }
  }

  // claim 対象インターフェース配下の Interrupt-IN エンドポイント(無ければ中止)
  uint16_t      maxPacketSize = 0;
  const uint8_t inEndpoint    = findInEndpoint(interfaceNum, maxPacketSize);
  if (inEndpoint == 0 || maxPacketSize == 0) {
    releaseGamepadInterface();
    return false;
  }

  // Interrupt-IN 転送は wMaxPacketSize 単位で要求する(複数パケット分をまとめて要求すると、
  // 短いパケットを受けたときに USB Host の内部で停止する)
  if (usb_host_transfer_alloc(maxPacketSize, 0, &m_dataTransfer) != ESP_OK) {
    releaseGamepadInterface();
    return false;
  }
  m_dataTransfer->device_handle    = m_deviceHandle;
  m_dataTransfer->bEndpointAddress = inEndpoint;
  m_dataTransfer->callback         = transferCallback;
  m_dataTransfer->context          = nullptr;
  m_dataTransfer->num_bytes        = maxPacketSize;
  m_dataTransfer->timeout_ms       = 1000;
  if (usb_host_transfer_submit(m_dataTransfer) != ESP_OK) {
    releaseGamepadInterface();
    return false;
  }

  sendXboxLedCommand(interfaceNum);
  return true;
}

void PocoGamepad::releaseGamepadInterface() {
  if (m_dataTransfer) {
    usb_host_transfer_free(m_dataTransfer);
    m_dataTransfer = nullptr;
  }
  usb_host_interface_release(m_clientHandle, m_deviceHandle, m_claimedInterface);
}

void PocoGamepad::sendXboxLedCommand(uint8_t interfaceNum) {
  const uint8_t outEndpoint = findXboxOutEndpoint(interfaceNum);
  if (outEndpoint == 0) {
    return;  // Xbox 互換ではない、または OUT エンドポイントなし
  }
  usb_transfer_t* ledTransfer;
  if (usb_host_transfer_alloc(8, 0, &ledTransfer) != ESP_OK) {
    return;
  }
  // Xbox 360 の LED コマンド: {0x01, 0x03, LED 状態}(0x02 = プレイヤー 1 点灯)
  constexpr uint8_t kLedCommand[3] = {0x01, 0x03, 0x02};
  std::memcpy(ledTransfer->data_buffer, kLedCommand, sizeof(kLedCommand));
  ledTransfer->num_bytes        = sizeof(kLedCommand);
  ledTransfer->device_handle    = m_deviceHandle;
  ledTransfer->bEndpointAddress = outEndpoint;
  ledTransfer->callback         = outTransferCallback;
  ledTransfer->context          = nullptr;
  if (usb_host_transfer_submit(ledTransfer) != ESP_OK) {
    usb_host_transfer_free(ledTransfer);
  }
}

bool PocoGamepad::isGamepadDevice(uint8_t& interfaceNum) {
  if (!m_deviceHandle) {
    return false;
  }
  const usb_config_desc_t* configDesc;
  if (usb_host_get_active_config_descriptor(m_deviceHandle, &configDesc) != ESP_OK) {
    return false;
  }
  int                        offset   = 0;
  const usb_standard_desc_t* nextDesc = reinterpret_cast<const usb_standard_desc_t*>(configDesc);
  while ((nextDesc = usb_parse_next_descriptor(nextDesc, configDesc->wTotalLength, &offset)) != nullptr) {
    if (nextDesc->bDescriptorType != USB_B_DESCRIPTOR_TYPE_INTERFACE) {
      continue;
    }
    const auto* intfDesc = reinterpret_cast<const usb_intf_desc_t*>(nextDesc);
    // HID のゲームパッド(class 0x03。キーボード 0x01・マウス 0x02 は除く)
    if (intfDesc->bInterfaceClass == 0x03 &&
        intfDesc->bInterfaceProtocol != 0x01 &&
        intfDesc->bInterfaceProtocol != 0x02) {
      interfaceNum = intfDesc->bInterfaceNumber;
      return true;
    }
    // Xbox 互換(XInput)
    if (intfDesc->bInterfaceClass == 0xFF && intfDesc->bInterfaceSubClass == 0x5D) {
      interfaceNum = intfDesc->bInterfaceNumber;
      return true;
    }
  }
  return false;
}

uint8_t PocoGamepad::findInEndpoint(uint8_t interfaceNum, uint16_t& maxPacketSize) {
  const usb_config_desc_t* configDesc;
  if (usb_host_get_active_config_descriptor(m_deviceHandle, &configDesc) != ESP_OK) {
    return 0;
  }
  int                        offset   = 0;
  const usb_standard_desc_t* nextDesc = reinterpret_cast<const usb_standard_desc_t*>(configDesc);
  bool inTargetInterface = false;
  while ((nextDesc = usb_parse_next_descriptor(nextDesc, configDesc->wTotalLength, &offset)) != nullptr) {
    if (nextDesc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
      const auto* intfDesc = reinterpret_cast<const usb_intf_desc_t*>(nextDesc);
      inTargetInterface    = (intfDesc->bInterfaceNumber == interfaceNum && intfDesc->bAlternateSetting == 0);
    } else if (inTargetInterface && nextDesc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
      const auto* epDesc = reinterpret_cast<const usb_ep_desc_t*>(nextDesc);
      if ((epDesc->bEndpointAddress & 0x80) && (epDesc->bmAttributes & 0x03) == USB_TRANSFER_TYPE_INTR) {
        maxPacketSize = epDesc->wMaxPacketSize;
        return epDesc->bEndpointAddress;
      }
    }
  }
  return 0;
}

uint8_t PocoGamepad::findXboxOutEndpoint(uint8_t interfaceNum) {
  const usb_config_desc_t* configDesc;
  if (usb_host_get_active_config_descriptor(m_deviceHandle, &configDesc) != ESP_OK) {
    return 0;
  }
  int                        offset   = 0;
  const usb_standard_desc_t* nextDesc = reinterpret_cast<const usb_standard_desc_t*>(configDesc);
  bool inTargetInterface = false;
  while ((nextDesc = usb_parse_next_descriptor(nextDesc, configDesc->wTotalLength, &offset)) != nullptr) {
    if (nextDesc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
      const auto* intfDesc = reinterpret_cast<const usb_intf_desc_t*>(nextDesc);
      inTargetInterface    = (intfDesc->bInterfaceNumber == interfaceNum &&
                           intfDesc->bAlternateSetting == 0 &&
                           intfDesc->bInterfaceClass == 0xFF);
    } else if (inTargetInterface && nextDesc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
      const auto* epDesc = reinterpret_cast<const usb_ep_desc_t*>(nextDesc);
      if (((epDesc->bEndpointAddress & 0x80) == 0) && (epDesc->bmAttributes & 0x03) == USB_TRANSFER_TYPE_INTR) {
        return epDesc->bEndpointAddress;
      }
    }
  }
  return 0;
}

// ---- 転送の完了 ---------------------------------------------------------------

void PocoGamepad::transferCallback(usb_transfer_t* transfer) {
  PocoGamepad* self = s_instance;
  if (self == nullptr || self->m_shutdownRequested) {
    return;
  }
  if (transfer->status == USB_TRANSFER_STATUS_NO_DEVICE ||
      transfer->status == USB_TRANSFER_STATUS_CANCELED ||
      !self->m_deviceHandle) {
    return;
  }
  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes > 0) {
    if (auto decoded = PocoGamepadDecoder::decode(transfer->data_buffer, transfer->actual_num_bytes)) {
      self->storeState(*decoded);
    }
  }
  if (self->m_deviceHandle && !self->m_shutdownRequested) {
    usb_host_transfer_submit(transfer);
  }
}

void PocoGamepad::controlTransferCallback(usb_transfer_t* transfer) {
  usb_host_transfer_free(transfer);
}

void PocoGamepad::outTransferCallback(usb_transfer_t* transfer) {
  usb_host_transfer_free(transfer);
}
