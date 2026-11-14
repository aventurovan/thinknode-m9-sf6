// Symulator interfejsu: ten sam UITask.cpp i UiView.cpp co w firmware, z atrapami sieci i sprzętu.
// Wciska zaplanowane klawisze i zapisuje zrzuty ekranu (.ppm).
#include "UITask.h"
#include <MyMesh.h>
#include <SPIFFS.h>
#include <sys/stat.h>
unsigned long sim_millis = 1000;
EspClass ESP; SpiffsClass SPIFFS; std::vector<uint8_t> sim_keys;
static uint16_t last[GFX_W * GFX_H];
void m9lcdBlit(int, int, int, int, const uint16_t* px) { for (int i = 0; i < GFX_W * GFX_H; i++) last[i] = (uint16_t)((px[i] >> 8) | (px[i] << 8)); }

static void mk(std::vector<ContactInfo>& v, const char* name, uint8_t type, uint8_t k0, uint8_t k1, uint32_t ago, double lat, double lon, uint8_t flags = 0) {
  ContactInfo c; memset(&c, 0, sizeof(c)); strcpy(c.name, name); c.type = type; c.flags = flags; c.id.pub_key[0] = k0; c.id.pub_key[1] = k1; c.id.pub_key[2] = 0x11;
  c.lastmod = 1794580320UL - ago; c.gps_lat = (int32_t)(lat * 1e6); c.gps_lon = (int32_t)(lon * 1e6); c.out_path_len = 0xFF; v.push_back(c);
}
MyMesh::MyMesh() {
  memset(&ui_admin, 0, sizeof(ui_admin)); memset(ui_sent, 0, sizeof(ui_sent)); memset(&self_id, 0x5a, sizeof(self_id));
  ChannelDetails d; memset(&d, 0, sizeof(d)); strcpy(d.name, "Public"); chans.push_back(d); strcpy(d.name, "#poludnie"); chans.push_back(d);
  mk(cts, "Ania", ADV_TYPE_CHAT, 0x10, 0x01, 300, 49.83, 19.05, 1); mk(cts, "Marek SP9", ADV_TYPE_CHAT, 0x11, 0x02, 720, 49.9, 19.0);
  mk(cts, "RPT Skrzyczne", ADV_TYPE_REPEATER, 0xa3, 0xf2, 120, 49.6845, 19.0303, 1); mk(cts, "RPT Klimczok", ADV_TYPE_REPEATER, 0x77, 0xc1, 900, 49.7403, 19.0086);
  mk(cts, "RPT Równica", ADV_TYPE_REPEATER, 0x0b, 0x9e, 7200, 49.7167, 18.8500); mk(cts, "Pokój Beskidy", ADV_TYPE_ROOM, 0x20, 0x03, 90000, 0, 0);
  mk(cts, "Stacja pogodowa", ADV_TYPE_SENSOR, 0x30, 0x04, 4000, 49.8, 19.1); mk(cts, "Tomek kamper", ADV_TYPE_CHAT, 0x12, 0x05, 3700, 50.5, 19.9);
  cts[1].out_path_len = 0x42; cts[1].out_path[0] = 0xa3; cts[1].out_path[1] = 0xf2; cts[1].out_path[2] = 0x77; cts[1].out_path[3] = 0xc1;
  ui_heard[0] = cts[2]; ui_heard_at[0] = 1794580320UL - 60; ContactInfo n; memset(&n, 0, sizeof(n)); strcpy(n.name, "Wojtek T-Echo"); n.type = ADV_TYPE_CHAT; n.id.pub_key[0] = 0x44;
  ui_heard[1] = n; ui_heard_at[1] = 1794580320UL - 400; ui_heard_n = 2;
}
bool MyMesh::uiDiscoverStart() { ui_disc_n = 2; memset(ui_disc, 0, sizeof(ui_disc)); memcpy(ui_disc[0].key, cts[2].id.pub_key, 32); ui_disc[0].key_len = 32; ui_disc[0].snr_rx = 38; ui_disc[0].snr_tx = 32;
  ui_disc[1].key[0] = 0x99; ui_disc[1].key[1] = 0x01; ui_disc[1].key[2] = 0x02; ui_disc[1].key_len = 32; ui_disc[1].snr_rx = -14; ui_disc[1].snr_tx = -20; return true; }
bool MyMesh::uiAdminStatus() { struct __attribute__((packed)) St { uint16_t mv, queue; int16_t noise, rssi; uint32_t recv, sent, air, up, sf, sd, rf, rd; uint16_t err; int16_t snr; } st = { 4080, 1, -112, -87, 18422, 6310, 5400, 273600, 5000, 1310, 15000, 3422, 2, 38 };
  memcpy(ui_admin.status, &st, sizeof(st)); ui_admin.status_len = sizeof(st); ui_admin.status_ready = true; return true; }
bool MyMesh::uiAdminNeighbours(uint16_t offset) { uint16_t total = 15, n = offset == 0 ? 12 : 3; memcpy(ui_admin.nb, &total, 2); memcpy(ui_admin.nb + 2, &n, 2); memset(ui_admin.nb + 4, 0, 132);
  for (int i = 0; i < n; i++) { ui_admin.nb[4 + i * 11] = (uint8_t)(0x40 + offset + i); ui_admin.nb[4 + i * 11 + 10] = (uint8_t)(30 - 4 * (offset + i)); }
  ui_admin.nb[4] = 0x77; ui_admin.nb[5] = 0xc1; ui_admin.nb[6] = 0x11; ui_admin.nb_len = 4 + n * 11; ui_admin.nb_ready = true; return true; }
MyMesh the_mesh;

static mesh::MainBoard board; static MultiSerialInterface ser; static DisplayDriver disp; static SensorManager sens;
static UITask ui(&board, &ser);
static int shot = 0;
static void run(int ms = 300) { for (int t = 0; t < ms; t += 50) { sim_millis += 50; ui.loop(); } }
static void save(const char* name) {
  run();
  char fn[64]; snprintf(fn, sizeof(fn), "%02d-%s.ppm", shot++, name);
  FILE* f = fopen(fn, "wb"); fprintf(f, "P6\n%d %d\n255\n", GFX_W, GFX_H);
  for (int i = 0; i < GFX_W * GFX_H; i++) { uint16_t p = last[i]; uint8_t rgb[3] = { (uint8_t)(((p >> 11) & 31) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31) }; fwrite(rgb, 1, 3, f); }
  fclose(f);
}
static void keys(std::initializer_list<int> ks) { for (int k : ks) { sim_keys.push_back((uint8_t)k); run(100); } }
static void type(const char* s) { for (; *s; s++) { sim_keys.push_back((uint8_t)*s); run(60); } }
static void rx(const char* thread, const char* text, int hops) {
  the_mesh.ui_rx_hops = hops; the_mesh.ui_rx_hash_size = 2; the_mesh.ui_rx_path_bytes = hops * 2; the_mesh.ui_rx_direct = false;
  uint8_t p[6] = { 0xa3, 0xf2, 0x77, 0xc1, 0x55, 0x66 }; memcpy(the_mesh.ui_rx_path, p, 6);
  the_mesh.ui_rx_chan = the_mesh.uiChannelIndex(thread) >= 0;
  ui.newMsg(0, thread, text, 0);
}

#include "M9PktLog.h"
// przykładowe surowe pakiety do logu (advert repeatera, kanał, prywatna, ACK, trasa)
static void pkt(int kind, int n) {
  uint8_t b[200]; int i = 0;
  switch (kind % 5) {
    case 0: {                                        // advert repeatera z pozycją i nazwą
      b[i++] = (4 << 2) | 1; b[i++] = (1 << 6) | 2; b[i++] = 0x18; b[i++] = 0x29; b[i++] = 0x79; b[i++] = 0x15;
      for (int k = 0; k < 100; k++) b[i++] = (uint8_t)(k == 0 ? 0x18 : k * 7);
      b[i++] = 0x80 | 0x10 | 0x02; int32_t la = 49685000, lo = 19031000; memcpy(&b[i], &la, 4); i += 4; memcpy(&b[i], &lo, 4); i += 4;
      const char* nm = "RPT Skrzyczne"; memcpy(&b[i], nm, strlen(nm)); i += strlen(nm); break; }
    case 1:                                          // wiadomość na kanale (hash 0 = kanał z atrapy)
      b[i++] = (5 << 2) | 1; b[i++] = (1 << 6) | 3; for (int k = 0; k < 6; k++) b[i++] = (uint8_t)(0x18 + k * 0x31);
      b[i++] = 0x00; for (int k = 0; k < 40; k++) b[i++] = (uint8_t)k; break;
    case 2:                                          // prywatna, bezpośrednio
      b[i++] = (2 << 2) | 2; b[i++] = 0; b[i++] = 0x11; b[i++] = the_mesh.cts.empty() ? 0x22 : the_mesh.cts[0].id.pub_key[0]; for (int k = 0; k < 30; k++) b[i++] = (uint8_t)k; break;
    case 3:                                          // ACK
      b[i++] = (3 << 2) | 1; b[i++] = (1 << 6) | 1; b[i++] = 0x77; b[i++] = 0xc1; for (int k = 0; k < 4; k++) b[i++] = (uint8_t)k; break;
    default:                                         // nieznany typ 0x0E
      b[i++] = (14 << 2) | 1; b[i++] = 2; b[i++] = 0xaa; b[i++] = 0xbb; for (int k = 0; k < 20; k++) b[i++] = (uint8_t)k; break;
  }
  m9logAdd(the_mesh.getRTCClock()->getCurrentTime(), sim_millis, 9.5f - (n % 7) * 2, -71 - (n % 9) * 5, b, i);
}

int main(int argc, char** argv) {
  if (argc > 1 && strcmp(argv[1], "fuzz") == 0) {      // losowe klawisze: szukanie błędów pamięci i zawieszeń
    mkdir("/tmp/symfs", 0777);
    ui.begin(&disp, &sens, &the_mesh.prefs);
    run(3000);
    static const uint8_t K[] = { M9K_ENTER, M9K_ENTER, M9K_ENTER, M9K_UP, M9K_DOWN, M9K_DOWN, M9K_LEFT, M9K_RIGHT, M9K_RIGHT, M9K_BACK, M9K_HOME, M9K_MSG, M9K_F3, M9K_F4, M9K_MAP, M9K_BKSP, M9K_BKSP_LONG, M9K_CTRL,
                               'a', 'z', '#', '1', '9', '.', '-', ':', ' ', 'R', 'p', 't' };
    srand(argc > 2 ? atoi(argv[2]) : 1);
    for (int i = 0; i < 60000; i++) {
      sim_keys.push_back(K[rand() % sizeof(K)]);
      if (i % 37 == 0) rx(i % 2 ? "Public" : "Marek SP9", i % 2 ? "Ania: test wiadomości z polskimi znakami żółć" : "prywatna wiadomość testowa", i % 5);
      if (i % 501 == 0) the_mesh.ui_sent_changed = true;
      if (i % 23 == 0) pkt(i / 23, i);
      if (i % 7 == 0) { uint8_t rb[255]; int rl = 1 + rand() % 255; for (int q = 0; q < rl; q++) rb[q] = (uint8_t)rand(); if (i % 21 == 0) rb[0] = (uint8_t)((4 << 2) | 1); if (i % 28 == 0) { rb[0] = (uint8_t)((5 << 2) | 1); rb[1] = 0; rb[2] = 0; } m9logAdd(1794580320UL, sim_millis, 3, -90, rb, rl); }   // losowe (także spreparowane) pakiety w logu
      sim_millis += 40; ui.loop();
      if (i % 997 == 0) { sim_millis += 40000; ui.loop(); }
    }
    printf("fuzz ok, kontaktow %d, kanalow %d\n", (int)the_mesh.cts.size(), (int)the_mesh.chans.size());
    return 0;
  }
  if (argc > 1 && strcmp(argv[1], "logfuzz") == 0) {   // spreparowane pakiety: każdy oglądamy na liście i w szczegółach
    mkdir("/tmp/symfs", 0777); remove("/tmp/symfs/m9ui.cfg");
    ui.begin(&disp, &sens, &the_mesh.prefs);
    run(3000);
    srand(argc > 2 ? atoi(argv[2]) : 1);
    for (int round = 0; round < 40; round++) {
      for (int k = 0; k < 200; k++) {
        uint8_t rb[255]; int rl = 1 + rand() % 255; for (int q = 0; q < rl; q++) rb[q] = (uint8_t)rand();
        int kind = rand() % 6;                           // część z poprawnym nagłówkiem, żeby dojść do głębszego parsowania
        if (kind < 5) { static const uint8_t T[5] = { 4, 5, 2, 8, 6 }; rb[0] = (uint8_t)((T[kind] << 2) | 1 + rand() % 2); if (rl > 1) rb[1] = (uint8_t)((rand() % 3) << 6 | (rand() % 4)); if (kind == 1 && rl > 12) rb[2 + ((rb[1] >> 6) + 1) * (rb[1] & 63)] = 0; }
        m9logAdd(1794580320UL, sim_millis, 3, -90, rb, rl);
      }
      keys({ M9K_HOME, M9K_F3, M9K_DOWN, M9K_ENTER });
      for (int f = 0; f < 4; f++) { keys({ M9K_ENTER }); for (int k = 0; k < 205; k++) keys({ M9K_RIGHT }); keys({ M9K_BACK, M9K_RIGHT }); }
    }
    printf("logfuzz ok, pakietow %lu\n", (unsigned long)m9logTotal());
    return 0;
  }
  mkdir("/tmp/symfs", 0777); remove("/tmp/symfs/m9ui.cfg"); remove("/tmp/symfs/m9ui.msg"); remove("/tmp/symfs/m9ui.pw"); remove("/tmp/symfs/m9ui.mut");
  ui.begin(&disp, &sens, &the_mesh.prefs);
  run(3000);
  rx("Public", "Ania: Nowy repeater w Ustroniu działa, sprawdźcie", 3); rx("Marek SP9", "Jadę na Równicę, będę o 16", 2);
  save("start");
  keys({ M9K_DOWN, M9K_DOWN, M9K_RIGHT }); save("pasek-zaznaczony");
  keys({ M9K_ENTER }); save("wiadomosci");
  keys({ M9K_F3 }); save("menu-rozmowy");
  keys({ M9K_DOWN, M9K_ENTER }); run(3000); keys({ M9K_BACK }); save("wiadomosci-wyciszony");
  keys({ M9K_ENTER }); type("Slysze go ze Skrzycznego"); keys({ M9K_ENTER }); save("rozmowa");
  keys({ M9K_UP, M9K_F3 }); save("menu-wiadomosci");
  keys({ M9K_DOWN, M9K_DOWN, M9K_ENTER }); save("odpowiedz-ack");
  keys({ M9K_BKSP_LONG, M9K_UP, M9K_RIGHT, M9K_ENTER }); save("szczegoly-wiadomosci");
  keys({ M9K_MAP }); save("kontakty");
  keys({ M9K_UP }); save("kontakty-filtry");
  keys({ M9K_F4 }); save("repeatery"); keys({ M9K_F3 }); save("repeatery-sortowanie"); keys({ M9K_BACK, M9K_DOWN, M9K_ENTER, M9K_ENTER, M9K_ENTER }); run(500); keys({ M9K_DOWN, M9K_ENTER }); run(500); save("sasiedzi");
  for (int i = 0; i < 12; i++) keys({ M9K_DOWN });
  save("sasiedzi-koniec"); keys({ M9K_ENTER }); run(500); for (int i = 0; i < 6; i++) keys({ M9K_DOWN }); save("sasiedzi-nastepni");
  keys({ M9K_HOME, M9K_LEFT }); save("ustawienia");
  keys({ M9K_ENTER }); save("radio");
  keys({ M9K_ENTER }); save("radio-preset");
  keys({ M9K_BACK, M9K_DOWN, M9K_DOWN, M9K_ENTER }); save("radio-bw");
  keys({ M9K_BACK, M9K_BACK, M9K_RIGHT, M9K_ENTER }); save("bluetooth");
  keys({ M9K_BACK, M9K_LEFT, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_ENTER, M9K_DOWN, M9K_DOWN, M9K_DOWN }); save("ekran");
  keys({ M9K_ENTER }); save("blokada-wybor");
  keys({ M9K_DOWN, M9K_ENTER }); run(6000); run(70000); keys({ 'a' }); save("ekran-zablokowany");
  keys({ M9K_BACK }); save("ekran-zablokowany-wstecz"); keys({ M9K_ENTER }); save("odblokowany");
  keys({ M9K_HOME, M9K_MAP }); save("mapa");
  keys({ M9K_RIGHT, 'i' }); save("mapa-przesunieta");
  keys({ M9K_F3 }); save("mapa-menu");
  keys({ M9K_BACK, M9K_F4, M9K_DOWN, M9K_ENTER }); save("menu-repeatera-mapa");
  keys({ M9K_HOME, M9K_LEFT, M9K_RIGHT, M9K_ENTER, M9K_DOWN, M9K_DOWN, M9K_ENTER }); save("zegar");
  keys({ M9K_RIGHT, M9K_UP, M9K_RIGHT, M9K_RIGHT, M9K_DOWN }); save("zegar-zmiana");
  keys({ M9K_BACK, M9K_HOME, M9K_DOWN, M9K_DOWN, M9K_RIGHT, M9K_RIGHT }); save("pasek-podglad");
  // trasa wiadomości na mapie: odebrana (nadawca → repeatery → ja) i wysłana (kto powtórzył)
  keys({ M9K_HOME, M9K_MSG }); save("trasa-lista");
  keys({ M9K_ENTER, M9K_F3, M9K_DOWN, M9K_DOWN }); save("trasa-menu-odebranej");
  keys({ M9K_ENTER }); save("trasa-odebranej");
  keys({ M9K_BACK, M9K_HOME, M9K_MSG, M9K_UP, M9K_UP, M9K_DOWN, M9K_ENTER, M9K_F3, M9K_DOWN, M9K_DOWN }); save("trasa-menu-wyslanej");
  keys({ M9K_ENTER }); save("trasa-wyslanej");
  keys({ M9K_BACK, M9K_HOME, M9K_LEFT, M9K_UP, M9K_UP, M9K_UP, M9K_LEFT, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_RIGHT, M9K_ENTER, M9K_DOWN, M9K_ENTER }); save("aktualizacja");
  // płynne przewijanie długiej rozmowy i suwak
  for (int i = 0; i < 14; i++) { char t[200]; snprintf(t, sizeof(t), "Ania: wiadomość numer %d%s", i + 1, i % 3 == 0 ? " — dłuższa, żeby zajęła kilka wierszy w dymku i było widać, jak przewija się po pikselu, a nie po całej wiadomości" : ""); rx("Public", t, i % 4); }
  keys({ M9K_BACK, M9K_BACK, M9K_HOME, M9K_MSG }); for (int i = 0; i < 8; i++) keys({ M9K_UP }); keys({ M9K_ENTER }); save("przewijanie-dol");
  keys({ M9K_UP }); save("przewijanie-1");
  sim_keys.push_back(M9K_UP); sim_millis += 50; ui.loop(); sim_millis += 30; ui.loop(); { char fn[64]; snprintf(fn, sizeof(fn), "%02d-przewijanie-animacja.ppm", shot++); FILE* f = fopen(fn, "wb"); fprintf(f, "P6\n%d %d\n255\n", GFX_W, GFX_H); for (int i = 0; i < GFX_W * GFX_H; i++) { uint16_t p = last[i]; uint8_t rgb[3] = { (uint8_t)(((p >> 11) & 31) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31) }; fwrite(rgb, 1, 3, f); } fclose(f); }
  save("przewijanie-2");
  for (int i = 0; i < 40; i++) keys({ M9K_UP }); save("przewijanie-gora");
  keys({ M9K_F3 }); save("przewijanie-menu");
  extern int sim_wifi; sim_wifi = 3; keys({ M9K_BACK, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_DOWN, M9K_F3 }); save("menu-odebranej-wifi");
  keys({ M9K_BACK, M9K_BACK, M9K_HOME, M9K_MSG, M9K_F3 }); save("menu-rozmow");
  keys({ M9K_BACK, M9K_BACK, M9K_HOME, M9K_MSG }); for (int i = 0; i < 6; i++) rx("Public", "Ania: nowa wiadomość po przerwie, nieprzeczytana", 2); keys({ M9K_UP, M9K_UP, M9K_UP, M9K_UP, M9K_UP, M9K_UP, M9K_UP, M9K_UP, M9K_ENTER }); save("wejscie-nieprzeczytane");
  sim_wifi = 1; save("wifi-slabe"); sim_wifi = 0; save("wifi-laczy"); sim_wifi = 5; save("wifi-ap");
  // nowe ekrany: Start, widmo, log pakietów, szczegóły, info o kanale, kod QR, bateria, ustawienia oszczędzania
  for (int i = 0; i < 30; i++) { pkt(i, i); sim_millis += 9000; ui.loop(); }
  keys({ M9K_ENTER, M9K_BACK, M9K_ENTER }); keys({ M9K_BACK, M9K_BACK, M9K_BACK, M9K_HOME }); save("n-start");
  keys({ M9K_DOWN, M9K_RIGHT, M9K_ENTER }); run(2000); save("n-widmo-pasmo");
  keys({ M9K_DOWN }); run(2000); save("n-widmo-zakres2");
  keys({ M9K_RIGHT }); save("n-widmo-kanal");
  keys({ M9K_F3 }); save("n-log");
  keys({ M9K_DOWN, M9K_DOWN, M9K_ENTER }); save("n-log-szczegoly");
  keys({ M9K_LEFT }); save("n-log-szczegoly-advert");
  keys({ M9K_BACK, M9K_RIGHT }); save("n-log-adverty");
  keys({ M9K_ENTER }); save("n-advert-szczegoly"); keys({ M9K_BACK });
  keys({ M9K_HOME, M9K_MSG, M9K_F3 }); save("n-menu-kanalu");
  keys({ M9K_DOWN, M9K_ENTER }); save("n-info-kanalu");
  keys({ M9K_ENTER }); save("n-kod-qr");
  keys({ M9K_HOME, M9K_RIGHT, M9K_RIGHT }); keys({ M9K_HOME, M9K_DOWN, M9K_RIGHT, M9K_RIGHT, M9K_ENTER }); save("n-bateria");
  keys({ M9K_HOME, M9K_F3 }); save("n-menu-start");
  printf("zrzutow: %d\n", shot);
  return 0;
}
