#include "M9Ble.h"

volatile uint32_t m9ble_connects = 0, m9ble_auth_ok = 0, m9ble_auth_fail = 0, m9ble_rx = 0, m9ble_tx = 0;
volatile uint8_t m9ble_fail_reason = 0;

#if defined(ESP32)
#include <esp_gap_ble_api.h>
#include <stdlib.h>
#include <Arduino.h>
#include <BLEDevice.h>

// true = oddaj pamięć Bluetooth do puli (kilkadziesiąt kB dla WiFi i szyfrowania); Bluetooth wraca dopiero po restarcie
void m9bleShutdown() { BLEDevice::deinit(true); delay(200); }
unsigned m9FreeHeapKB() { return ESP.getFreeHeap() / 1024; }

int m9bleBondCount() { return esp_ble_get_bond_device_num(); }

int m9bleClearBonds() {
  int n = esp_ble_get_bond_device_num();
  if (n <= 0) return 0;
  esp_ble_bond_dev_t* list = (esp_ble_bond_dev_t*)malloc(sizeof(esp_ble_bond_dev_t) * n);
  if (!list) return 0;
  int removed = 0;
  if (esp_ble_get_bond_device_list(&n, list) == ESP_OK) {
    for (int i = 0; i < n; i++) if (esp_ble_remove_bond_device(list[i].bd_addr) == ESP_OK) removed++;
  }
  free(list);
  return removed;
}
#else
int m9bleBondCount() { return 1; }
int m9bleClearBonds() { return 1; }
void m9bleShutdown() { }
unsigned m9FreeHeapKB() { return 90; }
#endif
