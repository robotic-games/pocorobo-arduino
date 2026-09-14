// ライブラリ内部用: ESP-IDF の戻り値を確認し、失敗ならログを出して false で抜ける
#pragma once

#include <Arduino.h>

#include "esp_err.h"

#define POCO_CHECK(expr, what)                             \
  do {                                                     \
    esp_err_t err_ = (expr);                               \
    if (err_ != ESP_OK) {                                  \
      log_e("%s failed: %s", what, esp_err_to_name(err_)); \
      return false;                                        \
    }                                                      \
  } while (0)
