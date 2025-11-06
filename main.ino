/*
  Fixed ESP32 sketch (safe version)
  - SSD1306 (I2C 0x3C) on SDA=21, SCL=22
  - Analog button ladder on D2 -> GPIO2
  - Menu: Scan / Packet Monitor / Deauth / Info
  - Scanner: scrollable SSID list
  - Packet monitor: promiscuous capture by channel with graph
  - Info screen: skull bitmap
  - SAFE: Radio initialized but no carrier or jamming functionality included
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <SPI.h>
#include <RF24.h>      
#include<radio_controller.h> // present but used only for safe init (no jamming)

// ---------------- Display ----------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------------- Buttons (analog ladder on D2) ----------------
const int ANALOG_PIN = 2;
const int BTN_UP_MIN     = 950;
const int BTN_UP_MAX     = 1250;
const int BTN_SELECT_MIN = 300;
const int BTN_SELECT_MAX = 550;
const int BTN_DOWN_MIN   = 1600;
const int BTN_DOWN_MAX   = 1950;
const unsigned long DEBOUNCE_MS = 120;
unsigned long lastBtnMs = 0;
int lastBtnState = 0;

// analog read averaging
int analogReadAvg(int pin, int samples = 6) {
  long sum = 0;
  for (int i=0;i<samples;i++){
    sum += analogRead(pin);
    delay(3);
  }
  return int(sum / samples);
}

// ---------------- Menu ----------------
const char* menu_options[] = {"Scan", "Packet_Monitor", "Deauth", "JAM"};
const int MENU_COUNT = 4;
const int ROW_Y[4] = {6, 20, 34, 48}; // fit inside 0..63
int menuIndex = 0;

// ---------------- Scanner ----------------
const int MAX_SSIDS = 60;
String ssids[MAX_SSIDS];
int ssidCount = 0;
int ssidScroll = 0;

// ---------------- Packet monitor ----------------
const int SAMPLE_WIDTH = 128;
uint32_t vals[SAMPLE_WIDTH];
const int CHANNEL_MAX = 11;
int monitorChannel = 1;
unsigned long pktCount = 0;
unsigned long deauthCount = 0;
unsigned long lastSampleMs = 0;
const unsigned long SAMPLE_INTERVAL_MS = 500;
unsigned long maxValue = 1;
double multiplier = 1.0;

bool inScanner = false;
bool inMonitor = false;
bool inInfo = false;

// ---------------- RF24 (safe init only) ----------------
// If you have your own RadioController, integrate it — DO NOT ENABLE JAMMING
#define CE_PIN  4
#define CSN_PIN 5
RF24 radio(CE_PIN, CSN_PIN);
const byte bluetooth_channels[] = {32, 34, 46, 48, 50, 52, 0, 1, 2, 4, 6, 8, 22, 24, 26, 28, 30, 74, 76, 78, 80};

// Example address placeholder (not used to transmit in this sketch)
byte address[6] = "node1";
RadioController rfController(radio, address);
// ---------------- Skull bitmap (16x16) ----------------
const unsigned char skull_bitmap [] PROGMEM = {
  0b00001111, 0b11110000,
  0b00011111, 0b11111000,
  0b00111111, 0b11111100,
  0b01111000, 0b00011110,
  0b01110000, 0b00001110,
  0b11100110, 0b01100111,
  0b11100110, 0b01100111,
  0b11100111, 0b11100111,
  0b11100111, 0b11100111,
  0b11100110, 0b01100111,
  0b01110000, 0b00001110,
  0b01111000, 0b00011110,
  0b00111111, 0b11111100,
  0b00011111, 0b11111000,
  0b00001111, 0b11110000,
  0b00000000, 0b00000000
};

// ---------------- Forward decls ----------------
void drawMainMenu();
int readButtonsDebounced();
void enterScanner();
void exitScanner();
void drawScanner();
void enterMonitor();
void exitMonitor();
void drawMonitor();
void resetMonitor();
void enterInfo();
void exitInfo();
void drawSkull(int x,int y);
void wifiPromiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  delay(50);

  Wire.begin(21,22);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)){
    Serial.println("SSD1306 init failed");
    while(1) delay(500);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  pinMode(ANALOG_PIN, INPUT);
  memset(vals, 0, sizeof(vals));

  // safe RF24 init (do not transmit)
 if (!rfController.begin()) { 
  Serial.println("Radio init failed!"); 
  while (true); 
  } 
  rfController.configureTX();
  Serial.println("Setup complete."); 
   
  drawMainMenu();
  Serial.println("Ready. Use buttons (ladder on D2).");
}

// ---------------- Loop ----------------
void loop() {
  int btn = readButtonsDebounced();

  if(!inScanner && !inMonitor && !inInfo) {
    if(btn == 1) { menuIndex = (menuIndex==0)?MENU_COUNT-1:menuIndex-1; drawMainMenu(); }
    if(btn == 3) { menuIndex = (menuIndex+1)%MENU_COUNT; drawMainMenu(); }
    if(btn == 2) { // SELECT
      switch(menuIndex) {
        case 0: enterScanner(); break;
        case 1: enterMonitor(); break;
        case 2:
          display.clearDisplay();
          display.setCursor(0,0);
          display.println("Deauth module");
          display.println("(placeholder - no transmit)");
          display.display();
          delay(800);
          drawMainMenu();
          break;
        case 3: enterInfo(); break;
      }
    }
  } 
  else if(inScanner) {
    if(btn == 1 && ssidScroll > 0) { ssidScroll--; drawScanner(); }
    if(btn == 3 && ssidScroll + 5 < ssidCount) { ssidScroll++; drawScanner(); }
    if(btn == 2) { exitScanner(); drawMainMenu(); }
  }
  else if(inInfo) {
    if(btn == 2) { exitInfo(); drawMainMenu(); }
  }

  delay(10);
}

// ---------------- Draw main menu ----------------
void drawMainMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // display.drawFastHLine(0, 0, 128, SSD1306_WHITE);
  // display.drawFastHLine(0, 20, 128, SSD1306_WHITE);
  // display.drawFastHLine(0, 40, 128, SSD1306_WHITE);
  // display.drawFastHLine(0, 60, 128, SSD1306_WHITE);

  for(int i=0;i<MENU_COUNT;i++){
    int y = ROW_Y[i];
    if(i==menuIndex){
     // display.fillRect(0, y-2, 128, 14, SSD1306_WHITE);
      const int HIGHLIGHT_H = 12;
      int yRect = max(0, y - 2);
      display.fillRect(0, yRect, 128, HIGHLIGHT_H, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(4, y);
    display.print(menu_options[i]);
    display.setTextColor(SSD1306_WHITE);
  }
  display.display();
}

// ---------------- Button reading + debounce (averaged) ----------------
int readButtonsDebounced(){
  int raw = analogReadAvg(ANALOG_PIN, 6);
  int btn = 0;
  if(raw >= BTN_UP_MIN && raw <= BTN_UP_MAX) btn = 1;
  else if(raw >= BTN_SELECT_MIN && raw <= BTN_SELECT_MAX) btn = 2;
  else if(raw >= BTN_DOWN_MIN && raw <= BTN_DOWN_MAX) btn = 3;
  else btn = 0;

  unsigned long now = millis();
  if(btn != lastBtnState) { lastBtnMs = now; lastBtnState = btn; return 0; }
  else {
    if(btn != 0 && (now - lastBtnMs) > DEBOUNCE_MS) { lastBtnState = -1; lastBtnMs = now; return btn; }
    if(btn == 0 && lastBtnState == -1) lastBtnState = 0;
  }
  return 0;
}

// ---------------- Scanner ----------------
void enterScanner(){
  inScanner = true;
  ssidScroll = 0;
  ssidCount = 0;

  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Scanning networks...");
  display.display();

  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect();
  delay(120);
  int n = WiFi.scanNetworks();
  ssidCount = min(n, MAX_SSIDS);
  for(int i=0;i<ssidCount;i++) ssids[i] = WiFi.SSID(i);
  drawScanner();
}

void exitScanner(){ inScanner = false; WiFi.scanDelete(); }

void drawScanner(){
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("WiFi Scanner");
  display.drawFastHLine(0,12,128,SSD1306_WHITE);

  int perPage = 5;
  for(int i=0;i<perPage;i++){
    int idx = ssidScroll + i;
    int y = 16 + i*10;
    display.setCursor(0,y);
    if(idx < ssidCount){
      String s = ssids[idx];
      if(s.length()>18) s = s.substring(0,18);
      display.print(idx+1); display.print(". "); display.print(s);
    } else {
      display.print("");
    }
  }
  display.display();
}

// ---------------- JAM ----------------

void jam() { 
  rfController.startCarrier(RF24_PA_MAX, 45);
   while(true){ 
    uint8_t newChannel = random(0, sizeof(bluetooth_channels) / sizeof(bluetooth_channels[0])); byte channel = bluetooth_channels[newChannel];
     rfController.setChannel(channel); } 
     }
void enterInfo(){
  inInfo = true;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  //display.println("");
  display.println("--------------");
  display.println("2.4GHZ Jammer");
  display.println("Elijah Cyber");
  display.println("Version 1.0");
  drawSkull(96, 40); // small skull bottom-right
  display.display();
  jam();
}

void exitInfo(){ inInfo = false; }

void drawSkull(int x,int y){
  display.drawBitmap(x,y, skull_bitmap, 16, 16, SSD1306_WHITE);
}

// ---------------- Packet monitor ----------------
void resetMonitor(){
  memset(vals,0,sizeof(vals));
  pktCount = 0;
  deauthCount = 0;
  maxValue = 1;
  multiplier = 1.0;
}

void enterMonitor(){
  inMonitor = true;
  lastSampleMs = millis();
  resetMonitor();

  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous_rx_cb(&wifiPromiscuousCb);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(monitorChannel, WIFI_SECOND_CHAN_NONE);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("Packet-Monitor");
  display.setCursor(0,16);
  display.println("Elijah Cyber");
  display.display();
  delay(800);

  while(inMonitor){
    int btn = readButtonsDebounced();
    if(btn == 1 && monitorChannel < CHANNEL_MAX){
      monitorChannel++;
      esp_wifi_set_channel(monitorChannel, WIFI_SECOND_CHAN_NONE);
      resetMonitor();
      delay(150);
    } else if(btn == 3 && monitorChannel > 1){
      monitorChannel--;
      esp_wifi_set_channel(monitorChannel, WIFI_SECOND_CHAN_NONE);
      resetMonitor();
      delay(150);
    } else if(btn == 2){
      exitMonitor();
      drawMainMenu();
      break;
    }

    unsigned long now = millis();
    if(now - lastSampleMs >= SAMPLE_INTERVAL_MS){
      lastSampleMs = now;
      memmove(vals, vals+1, (SAMPLE_WIDTH-1)*sizeof(vals[0]));
      vals[SAMPLE_WIDTH-1] = pktCount;
      if(pktCount > maxValue) maxValue = pktCount;
      multiplier = maxValue > 47 ? 47.0/(double)maxValue : 1.0;
      pktCount = 0;
      drawMonitor();
    }
    delay(8);
  }
}

void exitMonitor(){
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(NULL);
  inMonitor = false;
}

void drawMonitor(){
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("Packet-Monitor  Ch:"); display.print(monitorChannel);
  display.setCursor(0,10);
  display.print("Pkts/interval:"); display.print(vals[SAMPLE_WIDTH-1]);
  display.setCursor(80,0);
  display.print("Total:"); display.print(maxValue);

  for(int i=0;i<SAMPLE_WIDTH;i++){
    double scaled = vals[i]*multiplier;
    int h = (int)scaled;
    if(h>48) h=48;
    display.drawFastVLine(i,63-h,h,SSD1306_WHITE);
  }
  display.display();
}

// ---------------- Promiscuous callback ----------------
void wifiPromiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type){
  if(!inMonitor) return;
  wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t*)buf;
  pktCount++;
  if(pkt->rx_ctrl.sig_len > 12){
    uint8_t ctl = pkt->payload[12];
    if(ctl == 0xA0 || ctl == 0xC0) deauthCount++;
  }
}
