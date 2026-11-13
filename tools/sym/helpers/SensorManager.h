#pragma once
#include "sensors/LocationProvider.h"
class SensorManager { public: double node_lat = 49.8225, node_lon = 19.0469; LocationProvider lp; LocationProvider* getLocationProvider() { return &lp; } };
