#include "BLEManager.h"
#include "Config.h"
#include "NVSUtils.h"
#include <esp_mac.h>
#include <NimBLEDevice.h>

static uint8_t _activeSlot = 0;

// Set by the BLE security callback (NimBLE host task) when a bond has just
// completed and needs to be persisted. The actual flash write is done later
// from Bridge::loop() to avoid blocking the time-critical BLE stack.
// volatile because it's written on one task and read on another.
static volatile bool _pendingBondSave = false;

class MySecurityCallbacks : public NimBLESecurityCallbacks
{
  void onAuthenticationComplete(ble_gap_conn_desc *desc) override
  {
    if (desc->sec_state.bonded)
    {
      Serial.println("[BLE] Bonding Complete! Flagging NVS save for main loop...");
      _pendingBondSave = true;

      // Apple Accessory Design Guidelines require the connection interval to
      // be >= 15ms (i.e. >= 12 units of 1.25ms), otherwise iOS/macOS hosts
      // may reject the parameter update or the link can become unstable.
      // min=12 (15ms), max=24 (30ms), latency=0, timeout=400 (4s)
      if (NimBLEDevice::getServer() != nullptr)
      {
        NimBLEDevice::getServer()->updateConnParams(desc->conn_handle, 12, 24, 0, 400);
        Serial.println("[BLE] Connection interval set to 15-30ms (Apple-safe)");
      }
    }
  }

  // Passkey/PIN callbacks are unused with NoInputNoOutput ("Just Works")
  // IO capability, but NimBLE requires the virtual overrides to exist.
  uint32_t onPassKeyRequest() override { return 0; }
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

  // No display/keypad on this device: use "Just Works" pairing instead of a
  // hardcoded, always-accepted passkey. This still encrypts the link and
  // protects against passive eavesdropping (though not against an active
  // MITM during the very first pairing) without a fixed, guessable PIN.
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  static MySecurityCallbacks securityCallbacks; // static: no heap leak, lives for program lifetime
  NimBLEDevice::setSecurityCallbacks(&securityCallbacks);

  Serial.printf("[BLE] Advertising as '%s'\n", deviceName);
}

bool BLEManager::consumePendingBondSave()
{
  if (_pendingBondSave)
  {
    _pendingBondSave = false;
    return true;
  }
  return false;
}

bool BLEManager::isConnected()
{
  if (_bleCombo == nullptr)
  {
    return false;
  }
  return _bleCombo->isConnected();
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