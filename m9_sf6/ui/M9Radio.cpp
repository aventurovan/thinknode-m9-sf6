#include "M9Radio.h"
#include <Arduino.h>
#include <target.h>

extern RADIO_CLASS radio;                              // CustomLR1110 z target.cpp

static bool busy() {
  if (!radio_driver.isInRecvMode()) return true;       // nadaje albo nie słucha
  if (digitalRead(P_LORA_DIO_1) == HIGH) return true;  // czeka przerwanie (np. odebrany pakiet)
  uint32_t irq = radio.getIrqStatus();
  return (irq & (RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED | RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID)) != 0;   // trwa odbiór
}

int m9radioRssi() {
  if (busy()) return 0;
  float r = 0;
  if (radio.getRssiInst(&r) != RADIOLIB_ERR_NONE) return 0;
  int d = (int)(r - 0.5f);
  return d < -140 ? -140 : d > -1 ? -1 : d;
}

bool m9radioSweep(float f0, float step, int n, int8_t* out) {
  if (busy()) return false;
  float orig = radio.getFreqMHz();
  for (int i = 0; i < n; i++) {
    radio.standby();
    radio.setFrequency(f0 + step * (i + 0.5f), true);    // bez kalibracji obrazu (ta sama okolica pasma)
    radio.startReceive();
    delayMicroseconds(1500);                              // ustalenie się odczytu RSSI
    float r = -128;
    radio.getRssiInst(&r);
    out[i] = (int8_t)(r < -128 ? -128 : r > -1 ? -1 : r);
  }
  radio.standby();
  radio.setFrequency(orig, true);
  radio.clearIrqState(RADIOLIB_LR11X0_IRQ_ALL);
  radio.startReceive();                                   // z powrotem odbiór MeshCore na naszej częstotliwości
  return true;
}

int m9radioNoiseFloor() { return radio_driver.getNoiseFloor(); }
uint32_t m9radioAirtime(int len) { return radio_driver.getEstAirtimeFor(len); }

void m9cpuSave(bool on) {
  uint32_t want = on ? 80 : 240;
  if (getCpuFrequencyMhz() != want) setCpuFrequencyMhz(want);
}
int m9cpuMhz() { return (int)getCpuFrequencyMhz(); }
