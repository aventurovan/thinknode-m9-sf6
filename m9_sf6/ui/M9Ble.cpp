#include "M9Ble.h"
#if defined(ESP32)
#include <esp_gap_ble_api.h>
#include <stdlib.h>

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
#endif
