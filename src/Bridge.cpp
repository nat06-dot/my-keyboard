#include "Bridge.h"
#include "NVSUtils.h"
#include <hid_usage_keyboard.h>
#include <WiFi.h>

uint8_t Bridge::_currentSlot = 0;
BLEManager Bridge::_bleManager;
Preferences Bridge::_preferences;

// FreeRTOS Handlers
QueueHandle_t Bridge::_reportQueue = NULL;
TaskHandle_t Bridge::_bleTxTaskHandle = NULL;

void Bridge::begin()
{
  // 0. Disable WiFi immediately to isolate RF transceiver for BLE only (Zero RF Interference)
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // 1. Load saved slot
  _preferences.begin("usb-ble", true);
  _currentSlot = _preferences.getUChar("slot", 0);
  if (_currentSlot >= NUM_DEVICE_SLOTS)
    _currentSlot = 0;
  _preferences.end();

  // Startup Serial is fine as it only runs once and does not impact gaming performance
  Serial.printf("[Config] Starting on device slot %d\n", _currentSlot + 1);

  // 2. Load bonds for this slot
  NVSUtils::loadSlotBonds(_currentSlot);

  // 3. Init BLE
  const char *deviceNames[NUM_DEVICE_SLOTS] = {DEVICE_NAME_1, DEVICE_NAME_2,
                                               DEVICE_NAME_3};
  _bleManager.begin(_currentSlot, deviceNames[_currentSlot]);

  // 4. Create a Thread-safe FreeRTOS Queue for fast, non-blocking message passing (Depth: 8)
  _reportQueue = xQueueCreate(8, sizeof(KeyboardReport));
  if (_reportQueue == NULL)
  {
    Serial.println("[System] FATAL ERROR: Failed to create FreeRTOS Report Queue!");
  }

  // 5. Spawn Asynchronous BLE Transmission Task on Core 0 (Protocol Core)
  // This keeps BLE transmission and processing close to the NimBLE hardware stack context.
  xTaskCreatePinnedToCore(
      bleTxTask,         // Task Function
      "BLE_TX_Task",     // Name
      4096,              // Stack Size
      NULL,              // Parameter
      2,                 // Priority (Slightly elevated to prevent latency)
      &_bleTxTaskHandle, // Task Handle
      0                  // Core ID (Core 0 handles BLE stack)
  );

  // 6. Init USB (Runs on Core 1 / Application Core by default under Arduino)
  USBManager::setKeyboardCallback(onKeyboardReport);
  USBManager::begin();
}

void Bridge::loop()
{
  // ABSOLUTELY NO Serial logs here. This loop handles background connection events safely.
  static bool wasConnected = false;
  bool connected = _bleManager.isConnected();

  if (wasConnected && !connected)
  {
    NVSUtils::saveSlotBonds(_currentSlot);
  }
  wasConnected = connected;

  // Let FreeRTOS idle task breathe
  delay(10);
}

void Bridge::switchToSlot(uint8_t slot)
{
  if (slot >= NUM_DEVICE_SLOTS)
    return;

  if (slot == _currentSlot)
  {
    if (LED_FEEDBACK_PIN >= 0)
    {
      for (int i = 0; i <= slot; i++)
      {
        digitalWrite(LED_FEEDBACK_PIN, HIGH);
        delay(150);
        digitalWrite(LED_FEEDBACK_PIN, LOW);
        delay(150);
      }
    }
    return;
  }

  NVSUtils::saveSlotBonds(_currentSlot);

  _preferences.begin("usb-ble", false);
  _preferences.putUChar("slot", slot);
  _preferences.end();

  if (LED_FEEDBACK_PIN >= 0)
  {
    for (int i = 0; i <= slot; i++)
    {
      digitalWrite(LED_FEEDBACK_PIN, HIGH);
      delay(150);
      digitalWrite(LED_FEEDBACK_PIN, LOW);
      delay(150);
    }
  }

  usb_host_device_free_all();
  delay(500);
  ESP.restart();
}

void Bridge::onKeyboardReport(const uint8_t *data, size_t length)
{
  if (length < sizeof(hid_keyboard_input_report_boot_t))
    return;

  hid_keyboard_input_report_boot_t *kb_report =
      (hid_keyboard_input_report_boot_t *)data;

  // Check for device switching combo
  if (checkDeviceSwitchCombo(kb_report->key, kb_report->modifier.val))
  {
    return;
  }

  // ⚡ Step 1: Process SOCD (Last Win) on Core 1 (Blazing fast, ~1-2 microseconds!)
  applySOCD(kb_report->key);

  // ⚡ Step 2: Pack the processed state into a Queue structure
  KeyboardReport report;
  report.modifier = kb_report->modifier.val;
  memcpy(report.keys, kb_report->key, 6);

  // ⚡ Step 3: Push to FreeRTOS Queue (Non-blocking! Completes in ~1 microsecond)
  // This frees up the USB stack immediately to poll the keyboard at 1000Hz (no wait for BLE)
  if (_reportQueue != NULL)
  {
    xQueueSend(_reportQueue, &report, 0);
  }
}

bool Bridge::checkDeviceSwitchCombo(const uint8_t *keys, uint8_t modifiers)
{
  if (!ENABLE_DEVICE_SWITCHING)
    return false;

  bool hasCtrl = (modifiers & 0x11) != 0;
  uint8_t numberKey = 0;

  for (int i = 0; i < 6; i++)
  {
    if (keys[i] >= HID_KEY_1 && keys[i] <= HID_KEY_3)
    {
      numberKey = keys[i] - HID_KEY_1 + 1;
    }
  }

  if (hasCtrl && numberKey > 0 && numberKey <= NUM_DEVICE_SLOTS)
  {
    switchToSlot(numberKey - 1);
    return true;
  }

  return false;
}

void Bridge::applySOCD(uint8_t *keys)
{
  static bool prevLeft = false;
  static bool prevRight = false;
  static bool prevUp = false;
  static bool prevDown = false;

  static uint8_t lastHorizontalWinner = 0; // 1 = Left, 2 = Right
  static uint8_t lastVerticalWinner = 0;   // 1 = Up, 2 = Down

  int leftIdx = -1, rightIdx = -1;
  int upIdx = -1, downIdx = -1;

  for (int i = 0; i < 6; i++)
  {
    if (keys[i] == HID_KEY_A || keys[i] == HID_KEY_LEFT)
    {
      leftIdx = i;
    }
    if (keys[i] == HID_KEY_D || keys[i] == HID_KEY_RIGHT)
    {
      rightIdx = i;
    }
    if (keys[i] == HID_KEY_W || keys[i] == HID_KEY_UP)
    {
      upIdx = i;
    }
    if (keys[i] == HID_KEY_S || keys[i] == HID_KEY_DOWN)
    {
      downIdx = i;
    }
  }

  bool currLeft = (leftIdx != -1);
  bool currRight = (rightIdx != -1);
  bool currUp = (upIdx != -1);
  bool currDown = (downIdx != -1);

  if (currLeft && currRight)
  {
    if (!prevLeft && prevRight)
      lastHorizontalWinner = 1;
    else if (prevLeft && !prevRight)
      lastHorizontalWinner = 2;
    else if (!prevLeft && !prevRight)
      lastHorizontalWinner = 2;

    if (lastHorizontalWinner == 1)
      keys[rightIdx] = 0;
    else
      keys[leftIdx] = 0;
  }
  else
  {
    lastHorizontalWinner = 0;
  }

  if (currUp && currDown)
  {
    if (!prevUp && prevDown)
      lastVerticalWinner = 1;
    else if (prevUp && !prevDown)
      lastVerticalWinner = 2;
    else if (!prevUp && !prevDown)
      lastVerticalWinner = 2;

    if (lastVerticalWinner == 1)
      keys[downIdx] = 0;
    else
      keys[upIdx] = 0;
  }
  else
  {
    lastVerticalWinner = 0;
  }

  prevLeft = currLeft;
  prevRight = currRight;
  prevUp = currUp;
  prevDown = currDown;
}

// ⚡ Dedicated FreeRTOS Task running on Core 0 (Protocol Core)
// It blocks efficiently waiting for reports from the queue, then transmits them over BLE
void Bridge::bleTxTask(void *pvParameters)
{
  KeyboardReport report;
  for (;;)
  {
    // Block indefinitely until a new keyboard report is pushed to the queue
    if (xQueueReceive(_reportQueue, &report, portMAX_DELAY) == pdTRUE)
    {
      _bleManager.sendKeyboardReport(report.keys, report.modifier);
    }
  }
}
