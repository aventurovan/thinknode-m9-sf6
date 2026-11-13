#pragma once
// Bluetooth: zapamiętane parowania i liczniki diagnostyczne.
#include <stdint.h>
int  m9bleBondCount();      // ile telefonów jest zapamiętanych; -1 = nie da się odczytać
int  m9bleClearBonds();     // usuwa wszystkie; zwraca liczbę usuniętych
// liczniki zwiększane w SerialBLEInterface (łatka tools/patch_ble.py)
extern volatile uint32_t m9ble_connects, m9ble_auth_ok, m9ble_auth_fail, m9ble_rx, m9ble_tx;
extern volatile uint8_t m9ble_fail_reason;
