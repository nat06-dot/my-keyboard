#include "NVSUtils.h"
#include <Arduino.h>
#include <nvs.h>
#include <nvs_flash.h>

void NVSUtils::copyNamespace(const char *src_ns, const char *dst_ns)
{
  nvs_handle_t h_src, h_dst;
  esp_err_t err;

  // Try to open source namespace
  err = nvs_open(src_ns, NVS_READONLY, &h_src);
  if (err != ESP_OK)
  {
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
      // Source namespace doesn't exist at all (never created).
      // Nothing to copy, and no valid reason to touch destination.
      Serial.printf("[NVS] Source '%s' does not exist yet, skipping copy (destination untouched)\n", src_ns);
    }
    return;
  }

  // SAFETY CHECK: peek if source actually has any entries before doing
  // anything destructive to destination. This prevents a race where
  // copySlotBonds() is called too early (e.g. right when bonding
  // completes, before CCCD/subscribe data is written) and would
  // otherwise WIPE a previously-valid destination with an empty copy.
  nvs_iterator_t peek_it = nvs_entry_find("nvs", src_ns, NVS_TYPE_ANY);
  if (peek_it == NULL)
  {
    Serial.printf("[NVS] Source '%s' is empty right now, skipping copy to protect existing data in '%s'\n",
                  src_ns, dst_ns);
    nvs_close(h_src);
    return;
  }
  // (peek_it is freed automatically once exhausted/NULL by nvs_entry_next,
  // but since we're discarding it here without iterating, release it.)
  nvs_release_iterator(peek_it);

  // Open destination namespace
  err = nvs_open(dst_ns, NVS_READWRITE, &h_dst);
  if (err != ESP_OK)
  {
    nvs_close(h_src);
    return;
  }

  // Clear destination first to ensure exact copy
  nvs_erase_all(h_dst);

  size_t copied = 0, skipped = 0;

  // Iterate over all entries in source namespace
  nvs_iterator_t it = nvs_entry_find("nvs", src_ns, NVS_TYPE_ANY);
  while (it != NULL)
  {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);

    Serial.printf("NS=%s KEY=%s TYPE=%d\n",
                  info.namespace_name,
                  info.key,
                  info.type);

    // Copy based on type — every get is checked before the matching set
    switch (info.type)
    {
    case NVS_TYPE_U8:
    {
      uint8_t v;
      if (nvs_get_u8(h_src, info.key, &v) == ESP_OK &&
          nvs_set_u8(h_dst, info.key, v) == ESP_OK)
        copied++;
      else
        skipped++;
      break;
    }
    case NVS_TYPE_I8:
    {
      int8_t v;
      if (nvs_get_i8(h_src, info.key, &v) == ESP_OK &&
          nvs_set_i8(h_dst, info.key, v) == ESP_OK)
        copied++;
      else
        skipped++;
      break;
    }
    case NVS_TYPE_U16:
    {
      uint16_t v;
      if (nvs_get_u16(h_src, info.key, &v) == ESP_OK &&
          nvs_set_u16(h_dst, info.key, v) == ESP_OK)
        copied++;
      else
        skipped++;
      break;
    }
    case NVS_TYPE_I16:
    {
      int16_t v;
      if (nvs_get_i16(h_src, info.key, &v) == ESP_OK &&
          nvs_set_i16(h_dst, info.key, v) == ESP_OK)
        copied++;
      else
        skipped++;
      break;
    }
    case NVS_TYPE_U32:
    {
      uint32_t v;
      if (nvs_get_u32(h_src, info.key, &v) == ESP_OK &&
          nvs_set_u32(h_dst, info.key, v) == ESP_OK)
        copied++;
      else
        skipped++;
      break;
    }
    case NVS_TYPE_I32:
    {
      int32_t v;
      if (nvs_get_i32(h_src, info.key, &v) == ESP_OK &&
          nvs_set_i32(h_dst, info.key, v) == ESP_OK)
        copied++;
      else
        skipped++;
      break;
    }
    case NVS_TYPE_U64:
    {
      uint64_t v;
      if (nvs_get_u64(h_src, info.key, &v) == ESP_OK &&
          nvs_set_u64(h_dst, info.key, v) == ESP_OK)
        copied++;
      else
        skipped++;
      break;
    }
    case NVS_TYPE_I64:
    {
      int64_t v;
      if (nvs_get_i64(h_src, info.key, &v) == ESP_OK &&
          nvs_set_i64(h_dst, info.key, v) == ESP_OK)
        copied++;
      else
        skipped++;
      break;
    }
    case NVS_TYPE_STR:
    {
      size_t len = 0;
      bool ok = false;
      if (nvs_get_str(h_src, info.key, NULL, &len) == ESP_OK)
      {
        char *v = (char *)malloc(len);
        if (v)
        {
          if (nvs_get_str(h_src, info.key, v, &len) == ESP_OK &&
              nvs_set_str(h_dst, info.key, v) == ESP_OK)
          {
            ok = true;
          }
          free(v);
        }
      }
      ok ? copied++ : skipped++;
      break;
    }
    case NVS_TYPE_BLOB:
    {
      size_t len = 0;
      bool ok = false;
      if (nvs_get_blob(h_src, info.key, NULL, &len) == ESP_OK)
      {
        void *v = malloc(len);
        if (v)
        {
          if (nvs_get_blob(h_src, info.key, v, &len) == ESP_OK &&
              nvs_set_blob(h_dst, info.key, v, len) == ESP_OK)
          {
            ok = true;
          }
          free(v);
        }
      }
      ok ? copied++ : skipped++;
      break;
    }
    default:
      skipped++;
      break;
    }

    it = nvs_entry_next(it);
  }
  // No nvs_release_iterator call needed: per ESP-IDF docs, once
  // nvs_entry_find/nvs_entry_next return NULL the iterator is already freed.

  err = nvs_commit(h_dst);
  Serial.printf("[NVS] Copy '%s' -> '%s': %u ok, %u skipped, commit=%s\n",
                src_ns, dst_ns, (unsigned)copied, (unsigned)skipped,
                esp_err_to_name(err));

  nvs_close(h_src);
  nvs_close(h_dst);
}

void NVSUtils::loadSlotBonds(uint8_t slot)
{
  char slot_ns[16];
  snprintf(slot_ns, sizeof(slot_ns), "ble_bond_%d", slot);
  Serial.printf("[System] Loading BLE bonds for slot %d from '%s'...\n",
                slot + 1, slot_ns);
  copyNamespace(slot_ns, "nimble_bonds");
}

void NVSUtils::saveSlotBonds(uint8_t slot)
{
  char slot_ns[16];
  snprintf(slot_ns, sizeof(slot_ns), "ble_bond_%d", slot);
  Serial.printf("[System] Saving BLE bonds for slot %d to '%s'...\n", slot + 1,
                slot_ns);
  copyNamespace("nimble_bonds", slot_ns);
}

void NVSUtils::debugListAllEntries()
{
  Serial.println("========== [NVS DEBUG] Listing ALL entries in 'nvs' partition ==========");
  // namespace_name = NULL means "match any namespace"
  nvs_iterator_t it = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY);
  int count = 0;
  while (it != NULL)
  {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    Serial.printf("  NS='%s'  KEY='%s'  TYPE=%d\n",
                  info.namespace_name, info.key, info.type);
    count++;
    it = nvs_entry_next(it);
  }
  Serial.printf("========== [NVS DEBUG] Total entries found: %d ==========\n", count);
}