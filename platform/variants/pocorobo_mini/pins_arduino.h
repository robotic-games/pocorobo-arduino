// このファイルは後で自動生成に置き換える予定
// Pocorobo Mini のボード定義(Pocorobo 固有の定義だけを置く)
#pragma once

#include <stdint.h>

#define POCOROBO_BOARD_MINI 1
#define POCOROBO_BOARD_NAME "Pocorobo Mini"
#define POCOROBO_SERVO_COUNT   4
#define POCOROBO_MOTOR_COUNT   2
#define POCOROBO_ENCODER_COUNT 0
#define POCOROBO_HAS_BUZZER    0
#define POCOROBO_HAS_ENCODER   0
static const uint8_t PIN_SERVO[4]     = {1, 2, 4, 5};
static const uint8_t PIN_MOTOR_FWD[2] = {9, 38};  // 速度が正のとき PWM を出す側
static const uint8_t PIN_MOTOR_REV[2] = {10, 39};  // 速度が負のとき PWM を出す側
static const uint8_t PIN_LED    = 11;
static const uint8_t PIN_BUTTON = 42;
static const uint8_t PIN_USB_SELECT = 47;  // LOW: Type-C(パソコン) / HIGH: Type-A(ゲームパッド)
