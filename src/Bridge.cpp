#include "Bridge.h"
#include "NVSUtils.h"
#include <hid_usage_keyboard.h>

uint8_t Bridge::_currentSlot = 0;
BLEManager Bridge::_bleManager;
Preferences Bridge::_preferences;

void Bridge::begin()
{
  
  // 1. Load saved slot
  _preferences.begin("usb-ble", true);
  _currentSlot = _preferences.getUChar("slot", 0);
  if (_currentSlot >= NUM_DEVICE_SLOTS)
    _currentSlot = 0;
  _preferences.end();

  Serial.printf("[Config] Starting on device slot %d\n", _currentSlot + 1);

  // 2. Load bonds for this slot
  NVSUtils::loadSlotBonds(_currentSlot);

  // 3. Init BLE
  const char *deviceNames[NUM_DEVICE_SLOTS] = {DEVICE_NAME_1, DEVICE_NAME_2,
                                               DEVICE_NAME_3};
  _bleManager.begin(_currentSlot, deviceNames[_currentSlot]);

  // 4. Init USB
  USBManager::setKeyboardCallback(onKeyboardReport);
  USBManager::begin();
}

void Bridge::loop()
{
  static unsigned long lastStatus = 0;
  static bool wasConnected = false;

  bool connected = _bleManager.isConnected();

  if (wasConnected && !connected)
  {
    // Serial.println("[BLE] Client disconnected - syncing bonds to flash (Safe from CCCD loss)...");
    NVSUtils::saveSlotBonds(_currentSlot);
  }
  wasConnected = connected;

  if (millis() - lastStatus > 5000)
  {
    lastStatus = millis();
    bool connected = _bleManager.isConnected();
    // Serial.printf("[Status] Slot %d | BLE: %s\n", _currentSlot + 1,
    //               connected ? "CONNECTED" : "waiting for pairing...");
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
      delay(150);
      digitalWrite(LED_FEEDBACK_PIN, LOW);
      delay(150);
    }
  }

  usb_host_device_free_all();

  Serial.println("[System] Restarting to apply new slot settings...");
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

  // ⚡ ประมวลผลปุ่มทิศทางแบบ SOCD Last Win ก่อนส่งข้อมูลออกไป
  applySOCD(kb_report->key);

  // Debug output
  Serial.printf("[KB] mod:0x%02X keys:[%02X %02X %02X %02X %02X %02X]\n",
                kb_report->modifier.val, kb_report->key[0], kb_report->key[1],
                kb_report->key[2], kb_report->key[3], kb_report->key[4],
                kb_report->key[5]);

  // Forward to BLE
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
    if (keys[i] >= HID_KEY_1 && keys[i] <= HID_KEY_3)
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

// ฟังก์ชันกรองปุ่มทิศทางแบบสวนทางโดยให้ปุ่มกดทีหลังสุดชนะ (SOCD Cleaner - Last Input Priority)
void Bridge::applySOCD(uint8_t *keys)
{
  // ตัวแปร static เพื่อจดจำสถานะการกดในเฟรมก่อนหน้า
  static bool prevLeft = false;
  static bool prevRight = false;
  static bool prevUp = false;
  static bool prevDown = false;

  // ตัวแปรจำว่าปุ่มไหนเป็นตัวกดหลังสุด (1 = ฝั่งแรกชนะ, 2 = ฝั่งสองชนะ)
  static uint8_t lastHorizontalWinner = 0; // 1 = Left, 2 = Right
  static uint8_t lastVerticalWinner = 0;   // 1 = Up, 2 = Down

  int leftIdx = -1, rightIdx = -1;
  int upIdx = -1, downIdx = -1;

  // 1. ค้นหาดัชนีของปุ่มที่ถูกกดในรายงานปัจจุบัน (รองรับทั้ง WASD และปุ่มลูกศร)
  for (int i = 0; i < 6; i++)
  {
    if (keys[i] == HID_KEY_A || keys[i] == HID_KEY_LEFT)  { leftIdx = i; }
    if (keys[i] == HID_KEY_D || keys[i] == HID_KEY_RIGHT) { rightIdx = i; }
    if (keys[i] == HID_KEY_W || keys[i] == HID_KEY_UP)    { upIdx = i; }
    if (keys[i] == HID_KEY_S || keys[i] == HID_KEY_DOWN)  { downIdx = i; }
  }

  bool currLeft = (leftIdx != -1);
  bool currRight = (rightIdx != -1);
  bool currUp = (upIdx != -1);
  bool currDown = (downIdx != -1);

  // 2. จัดการฝั่งแนวนอน (ซ้าย - ขวา)
  if (currLeft && currRight)
  {
    // ตรวจจับว่าปุ่มไหนพึ่งโดนกดลงไปล่าสุดในเฟรมนี้
    if (!prevLeft && prevRight)
    {
      lastHorizontalWinner = 1; // ซ้ายกดทีหลัง -> ซ้ายชนะ
    }
    else if (prevLeft && !prevRight)
    {
      lastHorizontalWinner = 2; // ขวากดทีหลัง -> ขวาชนะ
    }
    else if (!prevLeft && !prevRight)
    {
      lastHorizontalWinner = 2; // ถ้าบังเอิญกดพร้อมกันในเฟรมเป๊ะๆ ให้ขวาชนะ
    }

    // ลบปุ่มผู้แพ้ออกจากรายงานส่งออก
    if (lastHorizontalWinner == 1)
    {
      keys[rightIdx] = 0; // ซ้ายชนะ ลบขวาออก
    }
    else
    {
      keys[leftIdx] = 0;  // ขวาชนะ ลบซ้ายออก
    }
  }
  else
  {
    lastHorizontalWinner = 0; // หากไม่ได้กดพร้อมกัน ให้รีเซ็ตค่าผู้ชนะ
  }

  // 3. จัดการฝั่งแนวตั้ง (บน - ล่าง)
  if (currUp && currDown)
  {
    // ตรวจจับว่าปุ่มไหนพึ่งโดนกดลงไปล่าสุดในเฟรมนี้
    if (!prevUp && prevDown)
    {
      lastVerticalWinner = 1; // บนกดทีหลัง -> บนชนะ
    }
    else if (prevUp && !prevDown)
    {
      lastVerticalWinner = 2; // ล่างกดทีหลัง -> ล่างชนะ
    }
    else if (!prevUp && !prevDown)
    {
      lastVerticalWinner = 2; // ถ้าบังเอิญกดพร้อมกัน ให้ล่างชนะ
    }

    // ลบปุ่มผู้แพ้ออกจากรายงานส่งออก
    if (lastVerticalWinner == 1)
    {
      keys[downIdx] = 0; // บนชนะ ลบล่างออก
    }
    else
    {
      keys[upIdx] = 0;   // ล่างชนะ ลบบนออก
    }
  }
  else
  {
    lastVerticalWinner = 0; // หากไม่ได้กดพร้อมกัน ให้รีเซ็ตค่าผู้ชนะ
  }

  // 4. บันทึกสถานะปัจจุบันไว้เปรียบเทียบในเฟรมถัดไป
  prevLeft = currLeft;
  prevRight = currRight;
  prevUp = currUp;
  prevDown = currDown;
}