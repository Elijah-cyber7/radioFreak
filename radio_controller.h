#ifndef RADIO_CONTROLLER_H
#define RADIO_CONTROLLER_H

#include <Arduino.h>
#include <RF24.h>

class RadioController {
public:
  RadioController(RF24 &radio, byte* address)
    : _radio(radio), _address(address) {}

  bool begin() {
    if (!_radio.begin()) return false;
    _radio.setPALevel(RF24_PA_LOW);
    _radio.setDataRate(RF24_1MBPS);
    _radio.openWritingPipe(_address);
    _radio.stopListening();
    return true;
  }

  void setPowerLevel(rf24_pa_dbm_e level) {
    _radio.setPALevel(level);
  }

  void setChannel(uint8_t channel) {
    _radio.setChannel(channel);
  }

  bool send(const String &data) {
    bool ok = _radio.write(data.c_str(), data.length() + 1);
    Serial.printf("Sent: %s | Success: %s\n", data.c_str(), ok ? "yes" : "no");
    return ok;
  }

  void configureTX() {
  _radio.begin();
  _radio.setAutoAck(false);
  _radio.stopListening();
  _radio.setRetries(0, 0);
  _radio.setPALevel(RF24_PA_MAX, true);
  _radio.setDataRate(RF24_2MBPS);
  _radio.setCRCLength(RF24_CRC_DISABLED);
}

void startCarrier(rf24_pa_dbm_e level, uint8_t channel) {
  _radio.startConstCarrier(level, channel);
  Serial.printf("[TEST] Constant carrier started on ch %d\n", channel);
}

void stopCarrier() {
  _radio.stopConstCarrier();
  Serial.println("[TEST] Constant carrier stopped.");
}



private:
  RF24 &_radio;
  byte* _address;
};

#endif
