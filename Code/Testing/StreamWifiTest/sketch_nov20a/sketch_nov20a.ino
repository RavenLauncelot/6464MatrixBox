#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <FastLED.h>
#include <WiFi.h>
#include <WiFiUdp.h>

//matrix setup
#define PANEL_WIDTH 64
#define PANEL_HEIGHT 64  	// Panel height of 64 will required PIN_E to be defined.	
#define PIN_E 32
MatrixPanel_I2S_DMA *matrix = nullptr;

//wifi setup
const char* SSID = "TVPoliceSurvAP";
const char* PASSWORD = "PogFrame6";

//port setup
WiFiUDP videoPort;
const int PORT = 8888;
const int FRAME_SIZE = PANEL_WIDTH * PANEL_HEIGHT * 3;
const int PACKETCHUNKS = 24;
const int MAXPACKETSIZE = FRAME_SIZE / PACKETCHUNKS;

uint8_t packetChunks[PACKETCHUNKS][MAXPACKETSIZE];
uint8_t fullFrame[FRAME_SIZE];
int chunkCounter = 0;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  //config for matrix 
  HUB75_I2S_CFG mxconfig;
  mxconfig.mx_height = PANEL_HEIGHT;      // we have 64 pix heigh panels  
  mxconfig.mx_width = PANEL_WIDTH;
  mxconfig.gpio.e = PIN_E;  

  matrix = new MatrixPanel_I2S_DMA(mxconfig);

  if( not matrix->begin() )
      Serial.println("****** !KABOOM! I2S memory allocation failed ***********");

  matrix->setBrightness(255);

  matrix->setTextSize(1);
  matrix->setTextColor(matrix->color565(0,0,255));

  //config for wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);

  matrix->setCursor(1,1);
  matrix->print("Connecting");

  while (WiFi.status() != WL_CONNECTED){
    matrix->print(".");
    delay(100);
  }

  matrix->clearScreen();
  matrix->setCursor(1,1);
  matrix->setTextColor(matrix->color565(255,255,0));
  matrix->print("Connected!");

  Serial.print("Local Ip address: ");
  Serial.println(WiFi.localIP());

  //config for UDP
  if (videoPort.begin(8888) == 0){
    Serial.println("No available sockets");
  }

}

void loop() {

  // put your main code here, to run repeatedly:
  if (WiFi.status() == WL_CONNECTED){

    int packetSize = videoPort.parsePacket();

    if (packetSize == MAXPACKETSIZE){

      Serial.print("Some data was receveid yayy: ");
      Serial.println(packetSize);
      videoPort.read(packetChunks[chunkCounter], MAXPACKETSIZE);

      chunkCounter++;

      if (chunkCounter == PACKETCHUNKS){

        chunkCounter = 0;
        Serial.println("Full frame data received");

        //counters for bits of data and the chunk it's using 
        int i = 0;
        int j = 0;

        for (int x = 1; x < PANEL_WIDTH; x++) {
          for (int y = PANEL_HEIGHT; y > 0; y--) {

            uint8_t r = packetChunks[j][i++];
            uint8_t g = packetChunks[j][i++];
            uint8_t b = packetChunks[j][i++];

            matrix->drawPixel(x, y, matrix->color565(r, g, b));

            if (i == MAXPACKETSIZE){
              i = 0;
              j++;
            }

          }
        }

        Serial.println("Framem completed");
        delay(100);     
      }     
    }
  }

}
