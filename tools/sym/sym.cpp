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
  ui.newMsg(0, thread, text, 0);
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
      sim_millis += 40; ui.loop();
      if (i % 997 == 0) { sim_millis += 40000; ui.loop(); }
    }
    printf("fuzz ok, kontaktow %d, kanalow %d\n", (int)the_mesh.cts.size(), (int)the_mesh.chans.size());
    return 0;
  }
  mkdir("/tmp/symfs", 0777); remove("/tmp/symfs/m9ui.cfg"); remove("/tmp/symfs/m9ui.msg"); remove("/tmp/symfs/m9ui.pw");
  ui.begin(&disp, &sens, &the_mesh.prefs);
  run(3000);
  rx("Public", "Ania: Nowy repeater w Ustroniu działa, sprawdźcie", 3); rx("Marek SP9", "Jadę na Równicę, będę o 16", 2);
  save("start");
  keys({ M9K_DOWN, M9K_DOWN, M9K_RIGHT }); save("pasek-zaznaczony");
  keys({ M9K_ENTER }); save("wiadomosci");
  keys({ M9K_F3 }); save("menu-rozmowy");
  keys({ M9K_DOWN, M9K_ENTER }); run(3000); keys({ M9K_BACK }); save("wiadomosci-wyciszony");
  keys({ M9K_ENTER }); type("Slysze go ze Skrzycznego"); keys({ M9K_ENTER }); save("rozmowa");
  keys({ M9K_UP, M9K_RIGHT, M9K_ENTER }); save("szczegoly-wiadomosci");
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
  printf("zrzutow: %d\n", shot);
  return 0;
}
