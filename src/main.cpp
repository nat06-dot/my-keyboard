#include "Bridge.h"
#include "Config.h"
#include "NVSUtils.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <nvs_flash.h>

void setup()
{
  setCpuFrequencyMhz(240);
  Serial.begin(115200);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("╔════════════════════════════════════════════════╗");
  Serial.println("║  ESP32-S3 USB to BLE Keyboard Bridge           ║");
  Serial.println("║  Supports keyboard + multi-device              ║");
  Serial.println("╚════════════════════════════════════════════════╝");
  Serial.println();

  if (LED_FEEDBACK_PIN >= 0)
  {
    pinMode(LED_FEEDBACK_PIN, OUTPUT);
    digitalWrite(LED_FEEDBACK_PIN, LOW);
  }

  Bridge::begin();

  Serial.printf("[DEBUG] NimBLEDevice::getNumBonds() = %d\n",
                NimBLEDevice::getNumBonds());

  // NVSUtils::debugListAllEntries();

  // 🟢 [แก้ไขจุดที่ 3]: เอาบรรทัดล้างความจำ (deleteAllBonds) ออก เพื่อไม่ให้มันลบข้อมูลจับคู่ทุกครั้งที่รีสตาร์ท!

  Serial.println();
  Serial.println("╔════════════════════════════════════════════════╗");
  Serial.println("║  READY - Connect USB devices via hub           ║");
  Serial.println("╚════════════════════════════════════════════════╝");
  Serial.println();
}

void loop()
{
  Bridge::loop();
  delay(1);
}