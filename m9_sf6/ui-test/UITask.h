#pragma once
// ThinkNode M9 — WERSJA TESTOWA: zamiast interfejsu cztery strony testowe
// (klawiatura, karta SD + GPS, mapa z karty, wgrywanie mapy przez WiFi). Companion (Bluetooth, radio SF6) działa normalnie w tle.

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <Arduino.h>

#include <AbstractUITask.h>
#include <NodePrefs.h>
#include "M9Keyboard.h"
#include "M9Map.h"

#define KBT_HISTORY 28
#define M9KB_REG_KEY 2     // kod klawisza jest w rejestrze 0x02 (potwierdzone na V2), 0 = nic

class UITask : public AbstractUITask {
  enum { PAGE_KB = 0, PAGE_SD = 1, PAGE_MAP = 2, PAGE_WIFI = 3, PAGE_COUNT = 4 };

  DisplayDriver* _display;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  M9Keyboard _kb;
  M9Map _map;

  int _page;
  unsigned long _next_refresh, _next_poll, _next_scan, _backlight_until;
  int _msgcount;

  // klawiatura
  uint32_t _reads;
  int16_t _regs_irq[M9KB_NUM_REGS];
  int16_t _regs_now[M9KB_NUM_REGS];
  bool _have_irq_regs;
  int16_t _hist[KBT_HISTORY];
  int _hist_len;
  int16_t _last_key; int _same_count; unsigned long _last_key_ms;

  // karta SD
  bool _sd_ok, _sd_tested, _sd_pending, _sd_free_done;
  bool _wifi_failed;
  uint8_t _sd_type;
  uint32_t _sd_size_mb, _sd_free_mb, _sd_write_kbs, _sd_read_kbs;
  unsigned long _next_sd_try;

  // GPS
  bool _gps_detected;
  uint32_t _gps_probe_bytes;
  bool _gps_probe;

  // mapa
  bool _map_mem, _map_dirty, _map_follow;
  double _map_lat, _map_lon;
  int _map_zoom, _map_tiles;
  unsigned long _map_ms, _next_map;

  void addHistory(int16_t key);
  void handleKey(int16_t key);
  void nextPage();
  void mountSD();
  void testSD();
  bool gpsFix(double* lat, double* lon, long* sats);
  void renderKeyboard();
  void renderSD();
  void renderMap();
  void renderWifi();

public:
  UITask(mesh::MainBoard* board, MultiSerialInterface* serial);
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  // z AbstractUITask
  void msgRead(int msgcount) override { _msgcount = msgcount; }
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override { _msgcount = msgcount; }
  void notify(UIEventType t = UIEventType::none) override { }
  void loop() override;
};
