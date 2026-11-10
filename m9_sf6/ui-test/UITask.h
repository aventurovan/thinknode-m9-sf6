#pragma once
// ThinkNode M9 — WERSJA TESTOWA: zamiast interfejsu jeden ekran testu klawiatury.
// Companion (Bluetooth, radio SF6) działa normalnie w tle.

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <Arduino.h>

#include <AbstractUITask.h>
#include <NodePrefs.h>
#include "M9Keyboard.h"

#define KBT_HISTORY 18

class UITask : public AbstractUITask {
  DisplayDriver* _display;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  M9Keyboard _kb;

  unsigned long _next_refresh, _next_poll, _next_scan, _backlight_until;
  int _msgcount;
  uint32_t _reads;                       // liczba odczytów po przerwaniu
  int16_t _regs_irq[M9KB_NUM_REGS];      // rejestry odczytane zaraz po przerwaniu
  int16_t _regs_now[M9KB_NUM_REGS];      // rejestry odczytywane co sekundę
  bool _have_irq_regs;
  struct { int16_t r1, r5; } _hist[KBT_HISTORY];
  int _hist_len;

  void addHistory(int16_t r1, int16_t r5);
  void render();

public:
  UITask(mesh::MainBoard* board, MultiSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
    _next_refresh = _next_poll = _next_scan = _backlight_until = 0;
    _msgcount = 0; _reads = 0; _hist_len = 0; _have_irq_regs = false;
    _node_prefs = NULL;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  // z AbstractUITask
  void msgRead(int msgcount) override { _msgcount = msgcount; }
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override { _msgcount = msgcount; }
  void notify(UIEventType t = UIEventType::none) override { }
  void loop() override;
};
