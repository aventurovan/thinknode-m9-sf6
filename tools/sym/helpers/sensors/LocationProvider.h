#pragma once
class LocationProvider { public: bool valid = true; long satellitesCount() { return 9; } bool isValid() { return valid; } long getLatitude() { return 49822500; } long getLongitude() { return 19046900; } long getTimestamp() { return 1794580320L; } };
