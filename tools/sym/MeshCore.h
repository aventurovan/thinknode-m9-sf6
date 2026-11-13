#pragma once
#include "Arduino.h"
#define PUB_KEY_SIZE 32
#define MAX_PATH_SIZE 64
#define ADV_TYPE_NONE 0
#define ADV_TYPE_CHAT 1
#define ADV_TYPE_REPEATER 2
#define ADV_TYPE_ROOM 3
#define ADV_TYPE_SENSOR 4
#define MAX_CONTACTS 350
#define MAX_ANON_CONTACTS 8
#define MAX_GROUP_CHANNELS 40
namespace mesh {
  struct Identity { uint8_t pub_key[PUB_KEY_SIZE]; };
  struct LocalIdentity : Identity { };
  struct GroupChannel { uint8_t hash[1]; uint8_t secret[32]; };
  class MainBoard { public: virtual uint16_t getBattMilliVolts() { return 3980; } };
  class RTCClock { public: uint32_t t = 1794580320UL; uint32_t getCurrentTime() { return t + sim_millis / 1000; } void setCurrentTime(uint32_t v) { t = v - sim_millis / 1000; } };
}
