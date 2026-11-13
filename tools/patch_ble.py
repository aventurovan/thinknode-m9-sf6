#!/usr/bin/env python3
"""Dopisuje do SerialBLEInterface.cpp z MeshCore liczniki diagnostyczne (połączenia, parowania, ramki),
które firmware M9 pokazuje w Ustawienia › Bluetooth. Uruchamiane przez workflow na przypiętej wersji MeshCore."""
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
def rep(a, b):
    global s
    assert a in s, "nie znaleziono: " + a[:50]
    s = s.replace(a, b, 1)
rep('#include "esp_mac.h"', '#include "esp_mac.h"\n\n// M9: liczniki diagnostyczne (definicje w m9_sf6/ui/M9Ble.cpp)\nextern volatile uint32_t m9ble_connects, m9ble_auth_ok, m9ble_auth_fail, m9ble_rx, m9ble_tx;\nextern volatile uint8_t m9ble_fail_reason;')
rep('    deviceConnected = true;\n  } else {', '    deviceConnected = true;\n    m9ble_auth_ok++;\n  } else {\n    m9ble_auth_fail++; m9ble_fail_reason = cmpl.fail_reason;')
rep('  last_conn_id = param->connect.conn_id;', '  last_conn_id = param->connect.conn_id;\n  m9ble_connects++;')
rep('    if (xQueueSend(recv_queue, &frame, 0) != pdTRUE) {', '    m9ble_rx++;\n    if (xQueueSend(recv_queue, &frame, 0) != pdTRUE) {')
rep('    pTxCharacteristic->notify();', '    pTxCharacteristic->notify();\n    m9ble_tx++;')
open(p, "w", encoding="utf-8").write(s)
print("patch BLE: ok")
