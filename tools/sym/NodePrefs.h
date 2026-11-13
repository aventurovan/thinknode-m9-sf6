#pragma once
#include <stdint.h>
struct NodePrefs {
  char node_name[32] = "M9-Paweł"; float freq = 869.618f, bw = 62.5f; uint8_t sf = 6, cr = 8, multi_acks = 0, manual_add_contacts = 0;
  int8_t tx_power_dbm = 22; uint32_t ble_pin = 0; uint8_t advert_loc_policy = 1, buzzer_quiet = 0, gps_enabled = 1, autoadd_config = 0, path_hash_mode = 1, autoadd_max_hops = 0;
};
