#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// CONFIGURATION - Edit these to customize behavior
// ============================================================================

/** @brief Device names for each slot (will show up in Bluetooth settings) */
#define DEVICE_NAME_1 "USB-BLE Dev 1"
#define DEVICE_NAME_2 "USB-BLE Dev 2"
#define DEVICE_NAME_3 "USB-BLE Dev 3"

#define DEVICE_MANUFACTURER "ESP32-S3"
#define BATTERY_LEVEL 100
#define NUM_DEVICE_SLOTS 3
#define ENABLE_DEVICE_SWITCHING true
#define LED_FEEDBACK_PIN 2

// 🟢 [เพิ่มตรงนี้]: ขาสำหรับควบคุมไฟ RGB (WS2812) บนบอร์ด ESP32-S3
// (ถ้าเป็นบอร์ด DevKit ทั่วไปมักจะเป็นพิน 48 หรือใช้ RGB_BUILTIN ของระบบ)
#ifdef RGB_BUILTIN
#define RGB_LED_PIN RGB_BUILTIN
#else
#define RGB_LED_PIN 48
#endif

#endif // CONFIG_H