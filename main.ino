#include <SPI.h>
#include <RF24.h>
#include "radio_controller.h"

// Pin definitions
#define CE_PIN  4
#define CSN_PIN 5

// Create RF24 instance
RF24 radio(CE_PIN, CSN_PIN);

// Address for TX/RX communication (5 bytes)
byte address[6] = "node1";


const byte bluetooth_channels[] = {32, 34, 46, 48, 50, 52, 0, 1, 2, 4, 6, 8, 22, 24, 26, 28, 30, 74, 76, 78, 80};


RadioController rfController(radio, address);

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 + nRF24 Modular Example");

  if (!rfController.begin()) {
    Serial.println("Radio init failed!");
    while (true);
  }
  rfController.configureTX();
  Serial.println("Setup complete.");
}

void jam()  {
  rfController.startCarrier(RF24_PA_MAX, 45);
  while(true){
    uint8_t newChannel = random(0, sizeof(bluetooth_channels) / sizeof(bluetooth_channels[0]));
    byte channel = bluetooth_channels[newChannel];
    rfController.setChannel(channel);

  }
}

void loop() {
  // Example: send data every 2 seconds on current channel
  jam();
  
}
