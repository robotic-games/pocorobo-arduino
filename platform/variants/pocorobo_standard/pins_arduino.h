// このファイルは後で自動生成に置き換える予定
// Pocorobo Standard のボード定義(Pocorobo 固有の定義だけを置く)
#pragma once

#include <stdint.h>

#define POCOROBO_BOARD_STANDARD 1
#define POCOROBO_BOARD_NAME "Pocorobo Standard"
#define POCOROBO_SERVO_COUNT   4
#define POCOROBO_MOTOR_COUNT   4
#define POCOROBO_ENCODER_COUNT 4
#define POCOROBO_HAS_BUZZER    1
#define POCOROBO_HAS_ENCODER   1
static const uint8_t PIN_SERVO[4]     = {1, 5, 4, 2};
static const uint8_t PIN_MOTOR_FWD[4] = {33, 35, 37, 48};  // 速度が正のとき PWM を出す側
static const uint8_t PIN_MOTOR_REV[4] = {34, 36, 38, 42};  // 速度が負のとき PWM を出す側
static const uint8_t PIN_ENCODER_A[4] = {17, 12, 10, 6};
static const uint8_t PIN_ENCODER_B[4] = {18, 13, 11, 7};
static const uint8_t PIN_LED    = 14;
static const uint8_t PIN_BUZZER = 39;
static const uint8_t PIN_BUTTON = 21;
static const uint8_t PIN_USB_SELECT = 47;  // LOW: Type-C(パソコン) / HIGH: Type-A(ゲームパッド)
