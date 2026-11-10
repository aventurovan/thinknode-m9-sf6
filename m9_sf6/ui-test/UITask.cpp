#include "UITask.h"
#include "../Version.h"

#ifndef M9_PIN_EXT_PWR
  #define M9_PIN_EXT_PWR 1      // LOW = podłączone zasilanie zewnętrzne
#endif

// wiersze tekstu: czcionka 16 px, współrzędne logiczne ekranu 128x64 (1 jednostka w pionie = 3,75 px)
#define ROW(n)  ((n) * 5)

static void hexList(char* dest, size_t size, const int16_t* v, int n) {
  dest[0] = 0;
  for (int i = 0; i < n; i++) {
    char t[6];
    if (v[i] < 0) strcpy(t, "-- "); else snprintf(t, sizeof(t), "%02X ", (unsigned)v[i]);
    strlcat(dest, t, size);
  }
}

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _node_prefs = node_prefs;

  pinMode(M9_PIN_EXT_PWR, INPUT);
  for (int i = 0; i < M9KB_NUM_REGS; i++) { _regs_irq[i] = -1; _regs_now[i] = -1; }

  _kb.begin();
  _backlight_until = millis() + 5000;
  _next_scan = millis() + 5000;

  Serial.printf("M9 test klawiatury %s: klawiatura %s\n", M9_FW_VERSION, _kb.isFound() ? "znaleziona" : "NIE znaleziona");
}

void UITask::addHistory(int16_t r1, int16_t r5) {
  if (_hist_len == KBT_HISTORY) {
    memmove(&_hist[0], &_hist[1], sizeof(_hist[0]) * (KBT_HISTORY - 1));
    _hist_len--;
  }
  _hist[_hist_len].r1 = r1;
  _hist[_hist_len].r5 = r5;
  _hist_len++;
}

void UITask::loop() {
  unsigned long now = millis();
  bool dirty = false;

  // naciśnięcie: przerwanie z klawiatury -> od razu czytamy wszystkie rejestry
  if (_kb.takeIrq()) {
    _kb.readRegs(_regs_irq, M9KB_NUM_REGS);
    _have_irq_regs = true;
    _reads++;
    addHistory(_regs_irq[1], _regs_irq[5]);
    _backlight_until = now + 5000;
    _kb.setBacklight(true);
    Serial.printf("KB irq #%lu:", (unsigned long)_kb.irqCount());
    for (int i = 0; i < M9KB_NUM_REGS; i++) Serial.printf(" R%d=%d", i, (int)_regs_irq[i]);
    Serial.println();
    dirty = true;
  }

  // co sekundę odczyt rejestrów bez przerwania (widać, co w nich zostaje między naciśnięciami)
  if ((long)(now - _next_poll) >= 0) {
    _next_poll = now + 1000;
    if (_kb.isFound()) _kb.readRegs(_regs_now, M9KB_NUM_REGS);
    dirty = true;
  }

  // klawiatury nie widać: ponawiamy skan co 5 s
  if (!_kb.isFound() && (long)(now - _next_scan) >= 0) {
    _next_scan = now + 5000;
    _kb.scan();
    dirty = true;
  }

  // podświetlenie klawiatury: włączone 5 s po naciśnięciu (test pinu KB_LED)
  if (_backlight_until && (long)(now - _backlight_until) >= 0) {
    _backlight_until = 0;
    _kb.setBacklight(false);
  }

  if (_display != NULL && (dirty || (long)(now - _next_refresh) >= 0)) {
    _next_refresh = now + 250;
    render();
  }
}

void UITask::render() {
  char line[96], tmp[64];
  DisplayDriver& d = *_display;

  d.startFrame();
  d.setTextSize(1);

  // 0: tytuł + wersja
  snprintf(line, sizeof(line), "M9 TEST KLAWIATURY  %s", M9_FW_VERSION);
  d.setCursor(0, ROW(0)); d.print(line);

  // 1-2: skan obu magistral I2C
  for (int b = 0; b < 2; b++) {
    tmp[0] = 0;
    for (int i = 0; i < _kb.num_found[b]; i++) {
      char t[6]; snprintf(t, sizeof(t), "%02X ", _kb.found[b][i]);
      strlcat(tmp, t, sizeof(tmp));
    }
    if (_kb.num_found[b] == 0) strcpy(tmp, "nic");
    snprintf(line, sizeof(line), "%s: %s", b == 0 ? "I2C A (SDA7 SCL6)" : "I2C B (SDA20 SCL21)", tmp);
    d.setCursor(0, ROW(1 + b)); d.print(line);
  }

  // 3: klawiatura
  if (_kb.isFound()) {
    snprintf(line, sizeof(line), "Klawiatura: 0x%02X na I2C %c (%s)", _kb.addr(), _kb.bus() == 0 ? 'A' : 'B',
             _kb.addr() == M9KB_ADDR_V1 ? "V1" : "V2");
  } else {
    strcpy(line, "Klawiatura: NIE ZNALEZIONA (szukam...)");
  }
  d.setCursor(0, ROW(3)); d.print(line);

  // 4: przerwania
  snprintf(line, sizeof(line), "KB_INT=%d  przerwan: %lu  odczytow: %lu", _kb.intLevel() ? 1 : 0,
           (unsigned long)_kb.irqCount(), (unsigned long)_reads);
  d.setCursor(0, ROW(4)); d.print(line);

  // 5: rejestry 0..7 zaraz po naciśnięciu
  if (_have_irq_regs) hexList(tmp, sizeof(tmp), _regs_irq, M9KB_NUM_REGS); else strcpy(tmp, "(nacisnij klawisz)");
  snprintf(line, sizeof(line), "Klawisz R0-7: %s", tmp);
  d.setCursor(0, ROW(5)); d.print(line);

  // 6: rejestry 0..7 teraz (co sekundę)
  hexList(tmp, sizeof(tmp), _regs_now, M9KB_NUM_REGS);
  snprintf(line, sizeof(line), "Teraz R0-7: %s", tmp);
  d.setCursor(0, ROW(6)); d.print(line);

  // 7-9: historia naciśnięć — pary R1/R5 (kod klawisza jest w jednym z nich)
  d.setCursor(0, ROW(7)); d.print(_hist_len == 0 ? "Historia (R1/R5): pusta" : "Historia (R1/R5), najnowsze na koncu:");
  for (int row = 0; row < 3; row++) {
    line[0] = 0;
    for (int i = row * 6; i < row * 6 + 6 && i < _hist_len; i++) {
      char a[4], b[4];
      if (_hist[i].r1 < 0) strcpy(a, "--"); else snprintf(a, sizeof(a), "%02X", (unsigned)_hist[i].r1);
      if (_hist[i].r5 < 0) strcpy(b, "--"); else snprintf(b, sizeof(b), "%02X", (unsigned)_hist[i].r5);
      snprintf(tmp, sizeof(tmp), "%s/%s  ", a, b);
      strlcat(line, tmp, sizeof(line));
    }
    d.setCursor(0, ROW(8 + row)); d.print(line);
  }

  // 11: bateria (ADC procesora) + zasilanie zewnętrzne; bateria wg klawiatury = rejestry 1..4 w wierszu „Teraz”
  snprintf(line, sizeof(line), "Bateria ADC: %u mV   zasilanie zewn.: %s", (unsigned)getBattMilliVolts(),
           digitalRead(M9_PIN_EXT_PWR) == LOW ? "TAK" : "nie");
  d.setCursor(0, ROW(11)); d.print(line);

  // 12: companion
  snprintf(line, sizeof(line), "BT %s  PIN 123456  %s", _connected ? "polaczony" : "czeka",
           _node_prefs ? _node_prefs->node_name : "");
  d.setCursor(0, 59); d.print(line);   // ostatni wiersz tuż nad dolną krawędzią (240 px)

  d.endFrame();
}
