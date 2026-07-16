/**
 * @file Bridge.h
 * @brief The application logic that bridges USB HID keyboard inputs to BLE HID outputs.
 * Uses FreeRTOS Queues and Dual-Core execution to achieve near-zero input lag.
 */

#ifndef BRIDGE_H
#define BRIDGE_H

#include "BLEManager.h"
#include "Config.h"
#include "USBManager.h"
#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// Struct to store keyboard report packets in the queue
struct KeyboardReport
{
  uint8_t modifier;
  uint8_t keys[6];
};

class Bridge
{
public:
  /**
   * @brief Initializes the application: disables WiFi, loads preferences,
   * starts the FreeRTOS Queue, spawns the BLE Task on Core 0, and starts USB Host.
   */
  static void begin();

  /**
   * @brief Main loop for handling non-time-critical connection events.
   */
  static void loop();

  /**
   * @brief Switches the active device slot, saves bonds, and restarts the ESP32.
   * @param slot New slot index (0-based).
   */
  static void switchToSlot(uint8_t slot);

private:
  static uint8_t _currentSlot;
  static BLEManager _bleManager;
  static Preferences _preferences;

  // FreeRTOS Core Synchronization
  static QueueHandle_t _reportQueue;
  static TaskHandle_t _bleTxTaskHandle;

  /** @brief Callback for processing USB keyboard reports. Runs on Core 1. */
  static void onKeyboardReport(const uint8_t *data, size_t length);

  /** @brief Checks if the current keyboard input matches the device switch combo. */
  static bool checkDeviceSwitchCombo(const uint8_t *keys, uint8_t modifiers);

<<<<<<< HEAD
  /** @brief Resolves simultaneous opposing direction inputs with Last Input Priority. */
  static void applySOCD(uint8_t *keys);

  /** @brief Dedicated FreeRTOS task running on Core 0 to transmit BLE reports. */
  static void bleTxTask(void *pvParameters);
=======
  static void applySOCD(uint8_t *keys);
>>>>>>> 1f84a68 (update_debug)
};

#endif // BRIDGE_H