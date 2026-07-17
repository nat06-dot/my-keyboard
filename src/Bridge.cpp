#include "Bridge.h"
#include "Config.h"
#include "NVSUtils.h"
#include <hid_usage_keyboard.h>

uint8_t Bridge::_currentSlot = 0;
BLEManager Bridge::_bleManager;
Preferences Bridge::_preferences;

static unsigned long lastGreenFlash = 0;
static bool isFlashingGreen = false;

static void setStatusRGB(uint8_t r, uint8_t g, uint8_t b)
{
#if defined(RGB_LED_PIN) && RGB_LED_PIN >= 0
  neopixelWrite(RGB_LED_PIN, r, g, b);
#endif
}

void Bridge::begin()
{
  setStatusRGB(64, 0, 0);

  USBManager::setKeyboardCallback(onKeyboardReport);
  USBManager::begin();

  _preferences.begin("usb-ble", true);
  _currentSlot = _preferences.getUChar("slot", 0);
  if (_currentSlot >= NUM_DEVICE_SLOTS)
    _currentSlot = 0;
  _preferences.end();

  Serial.printf("[Config] Starting on device slot %d\n", _currentSlot + 1);

  NVSUtils::loadSlotBonds(_currentSlot);

  const char *deviceNames[NUM_DEVICE_SLOTS] = {DEVICE_NAME_1, DEVICE_NAME_2,
                                               DEVICE_NAME_3};
  _bleManager.begin(_currentSlot, deviceNames[_currentSlot]);
}

void Bridge::loop()
{
  static unsigned long lastStatus = 0;
  static bool wasConnected = false;

  bool connected = _bleManager.isConnected();

  // The BLE security callback (NimBLE host task) only sets a flag when a
  // bond completes; the actual (slow, blocking) flash write happens here,
  // on the main loop, so it never stalls the BLE stack.
  if (_bleManager.consumePendingBondSave())
  {
    Serial.println("[BLE] Bonding complete - saving bonds to flash...");
    NVSUtils::saveSlotBonds(_currentSlot);
  }

  if (isFlashingGreen)
  {
    if (millis() - lastGreenFlash > 60)
    {
      isFlashingGreen = false;
    }
  }

  if (!isFlashingGreen)
  {
    if (connected)
    {
      setStatusRGB(0, 0, 64);
    }
    else
    {
      setStatusRGB(64, 0, 0);
    }
  }

  if (wasConnected && !connected)
  {
    Serial.println("[BLE] Client disconnected - syncing bonds to flash (Safe from CCCD loss)...");
    NVSUtils::saveSlotBonds(_currentSlot);
  }
  wasConnected = connected;

  if (millis() - lastStatus > 5000)
  {
    lastStatus = millis();
    Serial.printf("[Status] Slot %d | BLE: %s\n", _currentSlot + 1,
                  connected ? "CONNECTED" : "waiting for pairing...");
  }
}

void Bridge::switchToSlot(uint8_t slot)
{
  if (slot >= NUM_DEVICE_SLOTS)
    return;

  if (slot == _currentSlot)
  {
    Serial.printf("[BLE] Already on slot %d\n", slot + 1);
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

  Serial.printf("[BLE] Switching from slot %d to slot %d\n", _currentSlot + 1,
                slot + 1);

  NVSUtils::saveSlotBonds(_currentSlot);

  _preferences.begin("usb-ble", false);
  _preferences.putUChar("slot", slot);
  _preferences.end();

  if (LED_FEEDBACK_PIN >= 0)
  {
    for (int i = 0; i <= slot; i++)
    {
      digitalWrite(LED_FEEDBACK_PIN, HIGH);
      delay(50);
      digitalWrite(LED_FEEDBACK_PIN, LOW);
      delay(50);
    }
  }

  usb_host_device_free_all();

  Serial.println("[System] Restarting to apply new slot settings...");
  delay(100);
  ESP.restart();
}

void Bridge::onKeyboardReport(const uint8_t *data, size_t length)
{
  setStatusRGB(0, 64, 0);
  lastGreenFlash = millis();
  isFlashingGreen = true;

  const uint8_t *report_data = data;
  size_t report_length = length;

  if (length > 8 && (data[0] == 0x01 || data[0] == 0x02))
  {
    report_data = data + 1;
    report_length = length - 1;
  }

  if (report_length < sizeof(hid_keyboard_input_report_boot_t))
    return;

  hid_keyboard_input_report_boot_t *kb_report =
      (hid_keyboard_input_report_boot_t *)report_data;

  if (checkDeviceSwitchCombo(kb_report->key, kb_report->modifier.val))
  {
    return;
  }

  // 🟢 เอาคอมเมนต์ออกแล้ว! เรียกใช้ระบบประมวลผล SOCD Last Win ทันที
  applySOCD(kb_report->key);

  // Serial.printf("[KB] mod:0x%02X keys:[%02X %02X %02X %02X %02X %02X]\n",
  //               kb_report->modifier.val, kb_report->key[0], kb_report->key[1],
  //               kb_report->key[2], kb_report->key[3], kb_report->key[4],
  //               kb_report->key[5]);

  _bleManager.sendKeyboardReport(kb_report->key, kb_report->modifier.val);
}

bool Bridge::checkDeviceSwitchCombo(const uint8_t *keys, uint8_t modifiers)
{
  if (!ENABLE_DEVICE_SWITCHING)
    return false;

  bool hasCtrl = (modifiers & 0x11) != 0;
  uint8_t numberKey = 0;

  for (int i = 0; i < 6; i++)
  {
    if (keys[i] >= HID_KEY_1 && keys[i] <= (HID_KEY_1 + NUM_DEVICE_SLOTS - 1))
    {
      numberKey = keys[i] - HID_KEY_1 + 1;
    }
  }

  if (hasCtrl && numberKey > 0 && numberKey <= NUM_DEVICE_SLOTS)
  {
    Serial.printf("[Switch] Ctrl + %d detected\n", numberKey);
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
      leftIdx = i;
    if (keys[i] == HID_KEY_D || keys[i] == HID_KEY_RIGHT)
      rightIdx = i;
    if (keys[i] == HID_KEY_W || keys[i] == HID_KEY_UP)
      upIdx = i;
    if (keys[i] == HID_KEY_S || keys[i] == HID_KEY_DOWN)
      downIdx = i;
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