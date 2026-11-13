#pragma once
class MultiSerialInterface { public: bool bt = true; bool isBluetoothEnabled() { return bt; } void enableBluetooth() { bt = true; } void disableBluetooth() { bt = false; } };
