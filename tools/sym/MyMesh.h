#pragma once
// Atrapa MyMesh z przykładowymi danymi (kontakty, kanały, odpowiedzi repeatera).
#include <MeshCore.h>
#include <NodePrefs.h>
#include <vector>
struct ContactInfo { mesh::Identity id; char name[32]; uint8_t type, flags, out_path_len; uint8_t out_path[MAX_PATH_SIZE]; uint32_t last_advert_timestamp, lastmod; int32_t gps_lat, gps_lon; };
struct ChannelDetails { mesh::GroupChannel channel; char name[32]; };
class MyMesh {
public:
  mesh::RTCClock rtc; NodePrefs prefs; mesh::LocalIdentity self_id;
  std::vector<ContactInfo> cts; std::vector<ChannelDetails> chans;
  MyMesh();
  mesh::RTCClock* getRTCClock() { return &rtc; }
  int getTotalContactSlots() { return MAX_ANON_CONTACTS + (int)cts.size(); }
  int getNumContacts() { return (int)cts.size(); }
  bool getContactByIdx(int i, ContactInfo& c) { i -= MAX_ANON_CONTACTS; if (i < 0 || i >= (int)cts.size()) return false; c = cts[i]; return true; }
  bool getChannel(int i, ChannelDetails& d) { if (i < 0 || i >= MAX_GROUP_CHANNELS) return false; if (i < (int)chans.size()) d = chans[i]; else memset(&d, 0, sizeof(d)); return true; }
  uint32_t getBLEPin() { return 123456; }
  void savePrefs() { }
  bool advert() { return true; }
  bool uiRadioIsSF6() { return prefs.sf == 6 && prefs.cr == 8; }
  void uiSetRadioSF6() { uiSetRadio(869.618f, 62.5f, 6, 8); }
  void uiSetRadio(float f, float b, uint8_t s, uint8_t c) { prefs.freq = f; prefs.bw = b; prefs.sf = s; prefs.cr = c; }
  void uiSetTxPower(int8_t d) { prefs.tx_power_dbm = d; }
  int uiMaxTxPower() { return 22; }
  bool uiAdvertFlood() { return true; }
  void uiSetGps(bool on) { prefs.gps_enabled = on; }
  int uiChannelIndex(const char* n) { for (size_t i = 0; i < chans.size(); i++) if (strcmp(chans[i].name, n) == 0) return (int)i; return -1; }
  int uiAddHashtagChannel(const char* n) { ChannelDetails d; memset(&d, 0, sizeof(d)); strcpy(d.name, n); chans.push_back(d); return (int)chans.size() - 1; }
  bool uiRemoveChannel(const char* n) { int i = uiChannelIndex(n); if (i < 0) return false; chans.erase(chans.begin() + i); return true; }
  #define UI_SENT_MAX 12
  #define UI_REP_MAX 6
  struct UiSent { uint16_t id; uint8_t kind; uint8_t repeats; bool delivered; uint32_t ack; uint8_t hash[8]; uint8_t rep_n, rep_hs; uint8_t rep_id[UI_REP_MAX][3]; int8_t rep_snr[UI_REP_MAX]; };
  UiSent ui_sent[UI_SENT_MAX]; uint16_t seq = 0; bool ui_sent_changed = false;
  uint16_t uiSendChannelText(int, const char*) { UiSent& u = ui_sent[0]; memset(&u, 0, sizeof(u)); u.id = ++seq; u.kind = 1; u.repeats = 3; u.rep_n = 2; u.rep_hs = 2; u.rep_id[0][0] = 0xa3; u.rep_id[0][1] = 0xf2; u.rep_snr[0] = 38; u.rep_id[1][0] = 0x77; u.rep_id[1][1] = 0xc1; u.rep_snr[1] = -10; ui_sent_changed = true; return u.id; }
  uint16_t uiSendDirectText(const char*, const char*) { UiSent& u = ui_sent[1]; memset(&u, 0, sizeof(u)); u.id = ++seq; u.kind = 2; u.delivered = true; ui_sent_changed = true; return u.id; }
  UiSent* uiSent(uint16_t id) { for (int i = 0; i < UI_SENT_MAX; i++) if (ui_sent[i].id == id) return &ui_sent[i]; return NULL; }
  uint8_t ui_rx_path[48]; uint8_t ui_rx_path_bytes = 0, ui_rx_hash_size = 2, ui_rx_hops = 0; bool ui_rx_direct = false; int8_t ui_rx_snr_x4 = 30; int16_t ui_rx_rssi = -92;
  int uiToggleFavourite(const uint8_t* k) { for (auto& c : cts) if (memcmp(c.id.pub_key, k, 32) == 0) { c.flags ^= 1; return c.flags & 1; } return -1; }
  bool uiRemoveContact(const uint8_t* k) { for (size_t i = 0; i < cts.size(); i++) if (memcmp(cts[i].id.pub_key, k, 32) == 0) { cts.erase(cts.begin() + i); return true; } return false; }
  #define UI_HEARD_MAX 16
  ContactInfo ui_heard[UI_HEARD_MAX]; uint32_t ui_heard_at[UI_HEARD_MAX]; int ui_heard_n = 0;
  bool uiIsContact(const uint8_t* k) { for (auto& c : cts) if (memcmp(c.id.pub_key, k, 32) == 0) return true; return false; }
  bool uiAddHeard(int i) { cts.push_back(ui_heard[i]); return true; }
  struct UiAdmin { uint8_t key[32]; bool active; uint8_t login; bool is_admin; uint32_t status_tag; bool status_ready; uint8_t status[96]; uint8_t status_len; uint32_t nb_tag; bool nb_ready; uint8_t nb[160]; uint8_t nb_len; bool cli_wait, cli_ready; char cli[168]; uint32_t est_timeout; } ui_admin;
  bool uiAdminLogin(const uint8_t* k, const char*) { memset(&ui_admin, 0, sizeof(ui_admin)); memcpy(ui_admin.key, k, 32); ui_admin.active = true; ui_admin.login = 2; ui_admin.is_admin = true; return true; }
  bool uiAdminStatus();
  bool uiAdminNeighbours(uint16_t offset = 0);
  bool uiAdminCli(const char* c) { snprintf(ui_admin.cli, sizeof(ui_admin.cli), "odpowiedz na: %s — v1.9.1 (Build: 02-Nov-2026) to jest dluzsza odpowiedz ktora trzeba zawinac na kilka wierszy ekranu", c); ui_admin.cli_ready = true; return true; }
  void uiAdminLogout() { ui_admin.active = false; }
  #define UI_DISC_MAX 12
  struct UiDisc { uint8_t key[32]; uint8_t key_len; int8_t snr_rx, snr_tx, rssi; };
  UiDisc ui_disc[UI_DISC_MAX]; int ui_disc_n = 0;
  bool uiDiscoverStart();
  bool uiAddDiscovered(int i) { ContactInfo c; memset(&c, 0, sizeof(c)); memcpy(c.id.pub_key, ui_disc[i].key, 32); snprintf(c.name, sizeof(c.name), "RPT %02x%02x%02x", ui_disc[i].key[0], ui_disc[i].key[1], ui_disc[i].key[2]); c.type = ADV_TYPE_REPEATER; c.lastmod = 1794580320UL; cts.push_back(c); return true; }
  void uiReboot() { } bool uiFactoryReset() { return true; } void uiSetLatLon(double, double) { }
};
extern MyMesh the_mesh;
