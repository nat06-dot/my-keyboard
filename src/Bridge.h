#ifndef BRIDGE_H
#define BRIDGE_H

#include "BLEManager.h"
#include "Config.h"
#include "USBManager.h"
#include <Arduino.h>
#include <Preferences.h>

class Bridge
{
public:
  /**
   * @brief Initializes the application.
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

  /** @brief Callback for processing USB keyboard reports. */
  static void onKeyboardReport(const uint8_t *data, size_t length);

  /** @brief Checks if the current keyboard input matches the device switch combo. */
  static bool checkDeviceSwitchCombo(const uint8_t *keys, uint8_t modifiers);

  static void applySOCD(uint8_t *keys);
};

#endif // BRIDGE_H