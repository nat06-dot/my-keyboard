#include "Bridge.h"
#include "Config.h"
#include "NVSUtils.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <nvs_flash.h>

void setup()
{
  Serial.begin(115200);
  // delay(1000);
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

  // DEBUG: ดูว่ามี namespace/key อะไรอยู่ใน nvs partition บ้างก่อน bridge เริ่ม
  // NVSUtils::debugListAllEntries();

  Bridge::begin();

  Serial.printf("[DEBUG] NimBLEDevice::getNumBonds() = %d\n",
                NimBLEDevice::getNumBonds());

  // NimBLEDevice::deleteAllBonds();
  // Serial.println("Cleared all BLE bonds");

  Serial.println();
  Serial.println("╔════════════════════════════════════════════════╗");
  Serial.println("║  READY - Connect USB devices via hub           ║");
  Serial.println("╚════════════════════════════════════════════════╝");
  Serial.println();
}

void loop()
{
  Bridge::loop();
  delay(10);
}