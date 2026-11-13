#pragma once
// Interfejs „MC SF6” dla ThinkNode M9 — część sprzętowa: klawisze, dane z sieci MeshCore, ekrany.
// Rysowanie jest w UiView.cpp (przenośne), tu tylko wypełniamy UiView i reagujemy na klawisze.

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <Arduino.h>

#include <AbstractUITask.h>
#include <NodePrefs.h>
#include "M9Keyboard.h"
#include "UiView.h"

#define UI_MSG_MAX     120
#define UI_THREAD_MAX  48
#define UI_LIST_MAX    400

struct UiMsg {
  char thread[32];        // nazwa kanału albo kontaktu
  char who[24];           // nadawca (na kanale)
  char text[164];
  uint32_t ts;            // czas (zegar urządzenia)
  uint8_t chan, out, unread, hops;
  uint16_t sent_id;       // numer wysyłki (wiadomości własne)
  uint8_t repeats, delivered;
};

class UITask : public AbstractUITask {
  enum Scr { S_START = 0, S_MSGS = 1, S_CONTACTS = 2, S_RPTS = 3, S_SETTINGS = 4, S_RADIO = 5, S_THREAD, S_INFO, S_RPT, S_BOOT };
  enum InputMode { IN_MSG = 0, IN_CHANNEL = 1, IN_FREQ = 2, IN_NAME = 3, IN_TIME = 4 };
  struct Thread { char name[32]; uint8_t chan; uint8_t unread; uint32_t last; int16_t last_msg; };

  DisplayDriver* _display;
  SensorManager* _sensors;
  NodePrefs* _prefs;
  M9Keyboard _kb;
  genericBuzzer _buzzer;

  uint16_t* _fb;
  UiView* _v;
  UiMsg* _msgs; int _msg_n;
  Thread _threads[UI_THREAD_MAX]; int _thread_n;
  uint16_t* _list; int _list_n;         // indeksy kontaktów dla bieżącej listy

  uint8_t _scr, _back_scr;
  int _sel[6], _top[6];
  char _cur[32]; bool _cur_chan;        // otwarta rozmowa
  int _scroll;                          // ile najnowszych wiadomości pominąć (przewijanie)
  char _input[UI_INPUT_MAX * 2 + 4]; int _input_chars; bool _polish;
  uint8_t _rpt_key[32];
  uint8_t _input_mode;
  float _rf, _rbw; uint8_t _rsf, _rcr;   // ekran Radio: wartości przed „Zastosuj”
  uint8_t _confirm; char _confirm_name[32];

  bool _dirty, _awake;
  unsigned long _next_tick, _popup_until, _sleep_at, _boot_until;
  char _pop1[40], _pop2[64]; bool _pop_danger;
  int _msgcount;

  UiMsg* addMsg(const char* thread, const char* who, const char* text, bool chan, bool out, uint8_t hops);
  void buildThreads();
  void buildContacts(uint8_t kind);     // 0 = osoby i pokoje, 1 = repeatery
  void openThread(const char* name, bool chan);
  void markRead(const char* name, bool chan);
  int  unreadTotal();
  void sendInput();
  void popup(const char* a, const char* b = "", bool danger = false, int ms = 2500);
  void go(uint8_t scr);
  void wake();

  void key(uint8_t k);
  void keyList(uint8_t k, int count);
  void keyThread(uint8_t k);
  void keyRadio(uint8_t k);
  void openInput(uint8_t mode, const char* initial);
  void radioLoad();
  bool radioChanged();
  void fillRadio();
  void pollSent();
  bool timeFromGps();
  void enter();
  void back();

  void fill();                          // UiView z bieżącego stanu
  void fillStatus();
  void fillStart(); void fillMsgs(); void fillContacts(); void fillRpts(); void fillSettings(); void fillThread(); void fillInfo(); void fillRpt();
  void windowRows(int count, int scrIdx, int* first);
  void draw();

  void fmtAgo(char* dest, size_t n, uint32_t ts);
  void fmtClock(char* dest, size_t n, uint32_t ts, bool date_if_old);
  bool gpsFix(double* lat, double* lon, int* sats);

public:
  UITask(mesh::MainBoard* board, MultiSerialInterface* serial);
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;
  void notify(UIEventType t = UIEventType::none) override { _dirty = true; }
  void loop() override;
};
