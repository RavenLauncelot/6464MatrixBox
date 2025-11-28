#include <FastLED.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>

//matrix setup
#define PANEL_WIDTH 64
#define PANEL_HEIGHT 64
#define MATRIXPINE 32
MatrixPanel_I2S_DMA *matrix = nullptr;

//matrix setup extended (for streaming)
#define frameSize PANEL_WIDTH * PANEL_HEIGHT * 3
#define ChunkPerFrame 16
const uint16_t chunkSize ((frameSize / ChunkPerFrame) + 1); //plus one for the chunkindex
uint8_t frameBuffer[ChunkPerFrame][chunkSize-1]; //where the chunks are put together in order won't include the byte that says chunk number
uint8_t chunkNumber;

//UDP commands 
uint8_t packetContentsCMD[3]; //for incoming commands
const uint8_t pongCMD[3] = {192, 0, 0};
const uint8_t streamRequestAccept[3] = {
  48,
  (uint8_t)(chunkSize >> 8), //Shift the high bits down leaving the high 8 bits as 0
  (uint8_t)(chunkSize & 0xFF) //Apply a bit mask to set the High 8 bits as 0
};
const uint8_t chunkReceived[3] = {16,0,0};


//---- OLED ----
#define OLEDSDA 21
#define OLEDSCL 22
#define OLEDADDRESS 0x3C
#define OLEDWIDTH 128
#define OLEDHEIGHT 64
Adafruit_SSD1306 oledDisplay(OLEDWIDTH, OLEDHEIGHT, &Wire, -1);
unsigned long previousMillis = 0;

//for Oled refresh timer
const unsigned long oledRefreshTime = 200; //refreshes screen data every 10 seconds. Unless its a temp message which overrides this
unsigned long oledRefreshTimer = 0;

//for temp messages
unsigned long msgTimer = 0;


//for reconnecting effect
int ellipsesCounter = 0;
const unsigned long ellipsesDelay = 100;
unsigned long delayTimer = 0;
const int maxEllipses = 5;

//---- wifi setup ----
const char* SSID = "TVPoliceSurvAP";
const char* PASSWORD = "PogFrame6";
bool wasConnected = false;

//---- port setup ----
WiFiUDP udp;
const int PORT = 8888;
IPAddress deviceIp;

//---- stuff for idle anim ----
uint16_t time_counter = 0, cycles = 0, fps = 0;
unsigned long fps_timer;

CRGB currentColor;
CRGBPalette16 palettes[] = {HeatColors_p, LavaColors_p, RainbowColors_p, RainbowStripeColors_p, CloudColors_p};
CRGBPalette16 currentPalette = palettes[0];

CRGB ColorFromCurrentPalette(uint8_t index = 0, uint8_t brightness = 255, TBlendType blendType = LINEARBLEND) {
  return ColorFromPalette(currentPalette, index, brightness, blendType);
}

//---- main logic setup ----
enum Modes{
  IDLE,
  STREAMING
};

Modes mode = IDLE;

//---- declaring stuff early cus yeah ----
void IdleAnim();
void SendOLEDmsg(const char* message, float time);
void OLEDUpdate();
void IdleAnim();
void IdleLogic();
bool PacketCheck();
void StreamLogic();

void setup() {
  Serial.begin(115200);
  Wire.begin(OLEDSDA, OLEDSCL);

  Serial.println("Setup()");

  //OLED
  oledDisplay.begin(SSD1306_SWITCHCAPVCC, OLEDADDRESS);
  oledDisplay.clearDisplay();
  oledDisplay.display();
  oledDisplay.setTextColor(SSD1306_WHITE);
  oledDisplay.display();
  Serial.println("OLED SETUP DONE");

  //MATRIX
  HUB75_I2S_CFG mxconfig;
  mxconfig.mx_height = PANEL_HEIGHT;      
  mxconfig.mx_width = PANEL_WIDTH;
  mxconfig.gpio.e = MATRIXPINE; 
    
  matrix = new MatrixPanel_I2S_DMA(mxconfig);

  if(not matrix->begin()){
    Serial.println("****** !KABOOM! I2S memory allocation failed ***********");
  }
  matrix->setBrightness(255);

  Serial.println("MATRIX SETUP DONE");

  //WIFI
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);

  udp.begin(PORT);

  Serial.println("WIFI SETUP DONE");

  previousMillis = millis();
}

void loop() {
  // put your main code here, to run repeatedly:
  switch(mode){
    case 0:  //IDLE
      IdleAnim();
      IdleLogic();
      break;
    case 1: //STREAMING
      StreamLogic();
      break;
    default:
      mode = IDLE;
      break;
  }

  OLEDUpdateTimer();

  switch (WiFi.status()){
    case 3:
      if (!wasConnected){
        SendOLEDmsg("Connected!", 5000);
        wasConnected = true;
      }   
      break;
    case 4:

      break;
    case 5:
      if (wasConnected){
        SendOLEDmsg("Lost Connection!", 5000);
        wasConnected = false;
        mode = IDLE;
      }
      break;
    case 6:
      if (wasConnected){
        SendOLEDmsg("Lost Connection!", 5000);
        wasConnected = false;
        mode = IDLE;
      }
      break;
    default:
      break;

    //WL_NO_SHIELD        = 255,   // for compatibility with WiFi Shield library
    //WL_IDLE_STATUS      = 0,
    //WL_NO_SSID_AVAIL    = 1,
    //WL_SCAN_COMPLETED   = 2,
    //WL_CONNECTED        = 3,
    //WL_CONNECT_FAILED   = 4,
    //WL_CONNECTION_LOST  = 5,
    //WL_DISCONNECTED     = 6
  }
  
  oledDisplay.clearDisplay();
}

void SendOLEDmsg(const char* message, float time){

  msgTimer = (unsigned long)time;
  previousMillis = millis();

  oledDisplay.clearDisplay();
  oledDisplay.setTextSize(2);
  oledDisplay.setCursor(1,1);
  oledDisplay.println(message);
  oledDisplay.display();
}

//Oled update with tiumer so it only updates every 10 seconds or whatver is defined in oledRefreshTime
void OLEDUpdateTimer(){
  //check if a temp message is present
  if (msgTimer > 0){
    //usigned long will go positive if it goes negative so will check which is bigger.
    if (msgTimer < (millis() - previousMillis)){
      msgTimer = 0;
    }
    else{
      msgTimer -= (millis() - previousMillis);
    }

    Serial.print("Temp messagae time left: ");
    Serial.println(msgTimer);
  }

  //if no temp message is running do normal checks
  else{    
    if (oledRefreshTimer < (millis() - previousMillis)){
      oledRefreshTimer = oledRefreshTime; //reset timer

      OledUpdate();
    }

    else{
      oledRefreshTimer -= (millis() - previousMillis);
    }
  }
  
  previousMillis = millis();
}

void OledUpdate(){
  oledDisplay.clearDisplay();
  oledDisplay.setCursor(1,1);
  oledDisplay.setTextSize(1);
  oledDisplay.println("Hi gamers UwU");

  //Current mode display
  switch(mode){
    case 0: //ILDE 
      oledDisplay.println("Current mode: IDLE");
      break;
    case 1: //STREAMING
      oledDisplay.println("Current mode: STREAMNING");
      break;
    default:
      oledDisplay.println("Current mode: ERROR");
      break;
  }

  if (WiFi.status() == WL_CONNECTED){
    oledDisplay.print("Connected: ");
    oledDisplay.print(WiFi.RSSI());
    oledDisplay.println(" dB");
  }
  else{
    oledDisplay.print("Reconnecting");

    for (int i = 0; i < ellipsesCounter; i++){
      oledDisplay.print(".");
    }
    oledDisplay.println("");

    //usigned long will go positive if it goes negative so will check which is bigger.
    if (delayTimer < (millis() - previousMillis)){
      ellipsesCounter++;
      if (ellipsesCounter > maxEllipses){
        ellipsesCounter = 0;
      }
            
      //reset delay
      delayTimer = ellipsesDelay;
    }
    else{
      delayTimer -= (millis() - previousMillis);
    }
  }

  oledDisplay.display();
}

void IdleAnim(){
  for (int x = 0; x < PANEL_WIDTH; x++) {
    for (int y = 0; y <  PANEL_HEIGHT; y++) {
      int16_t v = 128;
      uint8_t wibble = sin8(time_counter);
      v += sin16(x * wibble * 3 + time_counter);
      v += cos16(y * (128 - wibble)  + time_counter);
      v += sin16(y * x * cos8(-time_counter) / 8);

      currentColor = ColorFromPalette(currentPalette, (v >> 8)); //, brightness, currentBlendType);
      matrix->drawPixelRGB888(x, y, currentColor.r, currentColor.g, currentColor.b);
    }
  }

  ++time_counter;
  ++cycles;
  ++fps;

  if (cycles >= 1024) {
    time_counter = 0;
    cycles = 0;
    currentPalette = palettes[random(0,sizeof(palettes)/sizeof(palettes[0]))];
  }

  // print FPS rate every 5 seconds
  // Note: this is NOT a matrix refresh rate, it's the number of data frames being drawn to the DMA buffer per second
  if (fps_timer + 5000 < millis()){
    Serial.printf_P(PSTR("Effect fps: %d\n"), fps/5);
    fps_timer = millis();
    fps = 0;
  }
   // end loop
}

int packetSize = 0;
void IdleLogic(){
  if (WiFi.status() == WL_CONNECTED){
    //check for connecting packets from python script

    packetSize = udp.parsePacket();

    if (packetSize > 0){
      udp.read(packetContentsCMD, 3);

      //ping cmd 
      if (packetContentsCMD[0] == 128){
        deviceIp = udp.remoteIP();
        Serial.print("Ping command from: ");
        Serial.println(deviceIp);

        udp.beginPacket(deviceIp, PORT);
        udp.write(pongCMD, 3);
        udp.endPacket();
      }

      //stream request
      if (packetContentsCMD[0] == 32){
        Serial.print("Stream request command received from: ");
        Serial.println(udp.remoteIP());
        udp.beginPacket(deviceIp, PORT);
        udp.write(streamRequestAccept, 3);
        udp.endPacket();

        mode = STREAMING;
      }
    }
  }

  else{
    //Do nothing cus you can't LOSERRRRRR
  }
}

int byteCounter = 0;
int chunk = 0;
void StreamLogic(){

  packetSize = udp.parsePacket();

  if (packetSize == chunkSize){
    //Serial.print("Chunk Number");
    //Serial.print(udp.peek());
    //Serial.println("Received");
    udp.read(&chunkNumber, 1);
    udp.read(frameBuffer[chunkNumber], chunkSize-1);
      
    chunk = 0;
    byteCounter = 0;
    //if all chunks sent draw frame
    if (chunkNumber == ChunkPerFrame-1){
      for (int x = 0; x < PANEL_WIDTH; x++){
        for (int y = 0; y < PANEL_HEIGHT; y++){   
          if (byteCounter >= chunkSize-1){  //Rememeber the chunk number is removed!!!!! hence minus one :3
            byteCounter = 0;
            chunk++;
          }

          matrix->drawPixelRGB888(x,y,frameBuffer[chunk][byteCounter],frameBuffer[chunk][byteCounter+1],frameBuffer[chunk][byteCounter+2]);
          byteCounter += 3;
          //me adding 3 makes me nervous but this wouldn't even wokr if i couldn't do this anyway

          //delay(10);
        }
      }
    }

    udp.beginPacket(deviceIp, PORT);
    udp.write(chunkReceived, 3);
    udp.endPacket();
  }
}
