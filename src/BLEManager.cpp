#include "BLEManager.h"
#include "Config.h"
#include "NVSUtils.h"
#include <esp_mac.h>
#include <NimBLEDevice.h>

static uint8_t _activeSlot = 0;

class MySecurityCallbacks : public NimBLESecurityCallbacks
{
  void onAuthenticationComplete(ble_gap_conn_desc *desc) override
  {
    if (desc->sec_state.bonded)
    {
      Serial.println("[BLE] Bonding Complete! Automatically saving NVS...");
      NVSUtils::saveSlotBonds(_activeSlot);
    }
  }
  uint32_t onPassKeyRequest() override { return 123456; }
  void onPassKeyNotify(uint32_t pass_key) override {}
  bool onConfirmPIN(uint32_t pass_key) override { return true; }
  bool onSecurityRequest() override
  {
    return true;
  }
};

BLEManager::BLEManager() : _bleCombo(nullptr) {}

void BLEManager::begin(uint8_t slot, const char *deviceName)
{
  _activeSlot = slot;

  setUniqueMac(slot);

  Serial.printf("[BLE] Initializing slot %d: '%s'\n", slot + 1, deviceName);
  _bleCombo = new BleCombo(deviceName, DEVICE_MANUFACTURER, BATTERY_LEVEL);
  _bleCombo->begin();

  NimBLEDevice::setSecurityCallbacks(new MySecurityCallbacks());

  Serial.printf("[BLE] Advertising as '%s'\n", deviceName);
}

bool BLEManager::isConnected()
{
  NimBLEServer *pServer = NimBLEDevice::getServer();
  if (pServer == nullptr)
  {
    return false;
  }
  return pServer->getConnectedCount() > 0;
}

void BLEManager::sendKeyboardReport(const uint8_t *keys, uint8_t modifiers)
{
  if (!isConnected())
  {
    return;
  }

  KeyReport report;
  report.modifiers = modifiers;
  report.reserved = 0;
  memcpy(report.keys, keys, 6);
  _bleCombo->sendReport(&report);
}

void BLEManager::setUniqueMac(uint8_t slot)
{
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  mac[5] = (mac[5] & 0xF0) | (slot & 0x0F);

  esp_err_t err = esp_base_mac_addr_set(mac);
  if (err != ESP_OK)
  {
    Serial.printf("[BLE] Failed to set MAC address: %s\n",
                  esp_err_to_name(err));
  }
  else
  {
    Serial.printf("[BLE] Set MAC address to: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }
}