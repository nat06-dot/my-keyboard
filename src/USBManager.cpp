#include "USBManager.h"
#include <hid_usage_keyboard.h>

KeyboardReportCallback USBManager::_keyboardCb = nullptr;

// กำหนดค่าเริ่มต้นเป็น NULL เผื่อกรณีตรวจสอบความปลอดภัย
static QueueHandle_t hid_host_event_queue = NULL;

typedef struct
{
  hid_host_device_handle_t hid_device_handle;
  hid_host_driver_event_t event;
  void *arg;
} hid_host_event_queue_t;

static const char *hid_proto_name_str[] = {"NONE", "KEYBOARD", "MOUSE"};

void USBManager::begin()
{
  // 🟢 [แก้ไขจุดที่ 1]: สร้าง Queue ขึ้นมาก่อนเริ่มใช้งานระบบ USB ป้องกันบอร์ด Crash!
  hid_host_event_queue = xQueueCreate(10, sizeof(hid_host_event_queue_t));
  if (hid_host_event_queue == NULL)
  {
    Serial.println("[USB] ❌ Failed to create HID host event queue!");
    return;
  }

  Serial.println("[USB] Installing USB Host library...");
  BaseType_t task_created =
      xTaskCreatePinnedToCore(usb_lib_task, "usb_events", 4096,
                              xTaskGetCurrentTaskHandle(), 2, NULL, 0);
  assert(task_created == pdTRUE);

  ulTaskNotifyTake(false, 1000);
  Serial.println("[USB] USB Host library ready");

  Serial.println("[USB] Installing HID driver...");
  const hid_host_driver_config_t hid_host_driver_config = {
      .create_background_task = true,
      .task_priority = 5,
      .stack_size = 4096,
      .core_id = 0,
      .callback = hid_host_device_callback,
      .callback_arg = NULL};
  ESP_ERROR_CHECK(hid_host_install(&hid_host_driver_config));

  task_created = xTaskCreate(&hid_host_task, "hid_task", 4096, NULL, 2, NULL);
  assert(task_created == pdTRUE);
  Serial.println("[USB] HID driver ready");
}

void USBManager::usb_lib_task(void *arg)
{
  const usb_host_config_t host_config = {
      .skip_phy_setup = false,
      .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };

  ESP_ERROR_CHECK(usb_host_install(&host_config));
  xTaskNotifyGive((TaskHandle_t)arg);

  while (true)
  {
    uint32_t event_flags;
    usb_host_lib_handle_events(portMAX_DELAY, &event_flags);

    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
    {
      usb_host_device_free_all();
    }
  }
}

void USBManager::hid_host_task(void *pvParameters)
{
  hid_host_event_queue_t evt_queue;

  while (true)
  {
    if (xQueueReceive(hid_host_event_queue, &evt_queue, pdMS_TO_TICKS(50)))
    {
      hid_host_device_event(evt_queue.hid_device_handle, evt_queue.event,
                            evt_queue.arg);
    }
  }
}

void USBManager::hid_host_device_callback(
    hid_host_device_handle_t hid_device_handle,
    const hid_host_driver_event_t event, void *arg)
{
  const hid_host_event_queue_t evt_queue = {
      .hid_device_handle = hid_device_handle, .event = event, .arg = arg};
  if (hid_host_event_queue != NULL)
  {
    xQueueSend(hid_host_event_queue, &evt_queue, 0);
  }
}

void USBManager::hid_host_device_event(
    hid_host_device_handle_t hid_device_handle,
    const hid_host_driver_event_t event, void *arg)
{
  hid_host_dev_params_t dev_params;

  if (hid_host_device_get_params(hid_device_handle, &dev_params) != ESP_OK)
  {
    return;
  }
  Serial.printf("[USB] Event=%d proto=%d subclass=%d iface=%d\n",
                event,
                dev_params.proto,
                dev_params.sub_class,
                dev_params.iface_num);

  const hid_host_device_config_t dev_config = {
      .callback = hid_host_interface_callback, .callback_arg = NULL};

  switch (event)
  {
  case HID_HOST_DRIVER_EVENT_CONNECTED:
    Serial.printf("[USB] %s connected!\n",
                  hid_proto_name_str[dev_params.proto]);

    if (dev_params.proto == HID_PROTOCOL_NONE)
    {
      Serial.println("[USB] Skipping NONE protocol device to save channels");
      break;
    }

    if (hid_host_device_open(hid_device_handle, &dev_config) != ESP_OK)
    {
      Serial.println("[USB] Failed to open HID device");
      break;
    }

    if (HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class)
    {
      hid_class_request_set_protocol(hid_device_handle,
                                     HID_REPORT_PROTOCOL_BOOT);
      if (HID_PROTOCOL_KEYBOARD == dev_params.proto)
      {
        hid_class_request_set_idle(hid_device_handle, 0, 0);
      }
    }

    if (hid_host_device_start(hid_device_handle) != ESP_OK)
    {
      Serial.println("[USB] Failed to start HID device");
    }
    break;

  default:
    break;
  }
}

void USBManager::hid_host_interface_callback(
    hid_host_device_handle_t hid_device_handle,
    const hid_host_interface_event_t event, void *arg)
{
  uint8_t data[64] = {0};
  size_t data_length = 0;
  hid_host_dev_params_t dev_params;

  if (hid_host_device_get_params(hid_device_handle, &dev_params) != ESP_OK)
  {
    return;
  }

  switch (event)
  {
  case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
  {
    esp_err_t err = hid_host_device_get_raw_input_report_data(
        hid_device_handle, data, sizeof(data), &data_length);

    if (err != ESP_OK)
    {
      Serial.printf("[USB] Failed to get raw input report data: %s\n",
                    esp_err_to_name(err));
      break;
    }

    if (_keyboardCb)
    {
      _keyboardCb(data, data_length);
    }
    break;
  }

  case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
    Serial.printf("[USB] %s disconnected\n",
                  hid_proto_name_str[dev_params.proto]);
    hid_host_device_close(hid_device_handle);
    break;

  case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
    Serial.printf("[USB] %s transfer error, closing device to recover...\n",
                  hid_proto_name_str[dev_params.proto]);
    hid_host_device_close(hid_device_handle);
    break;

  default:
    break;
  }
}