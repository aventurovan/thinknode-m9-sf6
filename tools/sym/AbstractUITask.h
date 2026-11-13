#pragma once
#include <MeshCore.h>
#include <helpers/MultiSerialInterface.h>
class genericBuzzer { public: void begin() { } void play(const char*) { } void loop() { } bool isPlaying() { return false; } void quiet(bool) { } };
enum class UIEventType { none, contactMessage, channelMessage, roomMessage, newContactMessage, ack };
class AbstractUITask {
protected:
  mesh::MainBoard* _board; MultiSerialInterface* _interfaceManager; bool _connected;
  AbstractUITask(mesh::MainBoard* b, MultiSerialInterface* i) : _board(b), _interfaceManager(i) { _connected = false; }
public:
  bool hasConnection() const { return _connected; }
  uint16_t getBattMilliVolts() const { return _board->getBattMilliVolts(); }
  bool isBluetoothEnabled() const { return _interfaceManager->isBluetoothEnabled(); }
  void enableBluetooth() { _interfaceManager->enableBluetooth(); }
  void disableBluetooth() { _interfaceManager->disableBluetooth(); }
  virtual void msgRead(int) = 0;
  virtual void newMsg(uint8_t, const char*, const char*, int) = 0;
  virtual void notify(UIEventType t = UIEventType::none) = 0;
  virtual void loop() = 0;
};
