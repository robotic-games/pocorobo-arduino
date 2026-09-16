// Pocorobo Standard のピン定義
// このファイルはロボットのファームウェアのビルド設定から自動生成される。手で編集しない。
#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>
#include "soc/soc_caps.h"

#define USB_VID 0x303a
#define USB_PID 0x1001

// ---- 機種 ----
#define POCOROBO_BOARD_STANDARD 1
#define POCOROBO_BOARD_NAME     "Pocorobo Standard"

// ---- 装置の個数と有無 ----
#define POCOROBO_SERVO_COUNT   4
#define POCOROBO_MOTOR_COUNT   4
#define POCOROBO_ENCODER_COUNT 4
#define POCOROBO_HAS_BUZZER    1
#define POCOROBO_HAS_ENCODER   1

// ---- 装置のピン ----
static const uint8_t PIN_SERVO[4]     = {1, 5, 4, 2};
static const uint8_t PIN_MOTOR_FWD[4] = {33, 35, 37, 48};  // 速度が正のとき PWM を出す側
static const uint8_t PIN_MOTOR_REV[4] = {34, 36, 38, 42};  // 速度が負のとき PWM を出す側
static const uint8_t PIN_ENCODER_A[4] = {17, 12, 10, 6};
static const uint8_t PIN_ENCODER_B[4] = {18, 13, 11, 7};
#define PIN_RGB_LED 14  // 本体のフルカラー LED
static const uint8_t PIN_LED        = PIN_RGB_LED;
static const uint8_t PIN_BUZZER     = 39;
static const uint8_t PIN_BUTTON     = 21;  // 本体のボタン
static const uint8_t PIN_USB_SELECT = 47;  // LOW: Type-C（パソコン） / HIGH: Type-A（ゲームパッド）
static const uint8_t PIN_USB_SENSE  = 8;  // Type-C の給電を ADC で読む（パソコンがつながっているかの判定用）
#define POCOROBO_USB_SENSE_THRESHOLD_MV 200  // これを超えたら「パソコンあり」（mV）

// ---- 拡張コネクタ ----
static const uint8_t SDA = 40;  // I2C コネクタ（3.3V。プルアップ抵抗は基板に無い）
static const uint8_t SCL = 41;
static const uint8_t TX = 43;  // UART コネクタ
static const uint8_t RX = 44;

// ---- Arduino 標準の名前 ----
static const uint8_t LED_BUILTIN = SOC_GPIO_PIN_COUNT + PIN_RGB_LED;
#define BUILTIN_LED    LED_BUILTIN
#define LED_BUILTIN    LED_BUILTIN
#define RGB_BUILTIN    LED_BUILTIN
#define RGB_BRIGHTNESS 64

// SPI の既定ピン（コネクタには出ていない。使うときは SPI.begin(sck, miso, mosi, ss) でピンを指定する）
static const uint8_t SS   = 3;
static const uint8_t MOSI = 16;
static const uint8_t MISO = 46;
static const uint8_t SCK  = 15;

static const uint8_t A0 = 1;
static const uint8_t A1 = 2;
static const uint8_t A2 = 3;
static const uint8_t A3 = 4;
static const uint8_t A4 = 5;
static const uint8_t A5 = 6;
static const uint8_t A6 = 7;
static const uint8_t A7 = 8;
static const uint8_t A8 = 9;
static const uint8_t A9 = 10;
static const uint8_t A10 = 11;
static const uint8_t A11 = 12;
static const uint8_t A12 = 13;
static const uint8_t A13 = 14;
static const uint8_t A14 = 15;
static const uint8_t A15 = 16;
static const uint8_t A16 = 17;
static const uint8_t A17 = 18;
static const uint8_t A18 = 19;
static const uint8_t A19 = 20;

static const uint8_t T1 = 1;
static const uint8_t T2 = 2;
static const uint8_t T3 = 3;
static const uint8_t T4 = 4;
static const uint8_t T5 = 5;
static const uint8_t T6 = 6;
static const uint8_t T7 = 7;
static const uint8_t T8 = 8;
static const uint8_t T9 = 9;
static const uint8_t T10 = 10;
static const uint8_t T11 = 11;
static const uint8_t T12 = 12;
static const uint8_t T13 = 13;
static const uint8_t T14 = 14;

#endif /* Pins_Arduino_h */
