#include <WiFiUdp.h>
#include <WiFi.h>

//---- wifi setup ----
const char* SSID = "TVPoliceSurvAP";
const char* PASSWORD = "PogFrame6";

WiFiUDP udp;
const int PORT = 8888;

uint8_t packetContentsCMD[3];

#define panelHeight 64
#define panelHeight 64
#define frameSize panelHeight * panelHeight * 3
#define ChunkPerFrame 16
const uint16_t chunkSize ((frameSize / ChunkPerFrame) + 1); //plus one for the chunkindex

const uint8_t pongCMD[3] = {192, 0, 0};
const uint8_t streamRequestAccept[3] = {
  48,
  (uint8_t)(chunkSize >> 8), //Shift the high bits down leaving the high 8 bits as 0
  (uint8_t)(chunkSize & 0xFF) //Apply a bit mask to set the High 8 bits as 0
};
const uint8_t chunkReceived[3] = {16,0,0};

uint8_t frameBuffer[ChunkPerFrame][chunkSize-1]; //where the chunks are put together in order won't include the byte that says chunk number

bool streamMode = false;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  //WIFI
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);

  udp.begin(PORT);

  Serial.println("Connecting");
  while(WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    delay(100);
  }
  Serial.println("");
  Serial.println("Connected!");
}


int packetSize = 0;
IPAddress deviceIp;
uint8_t chunkNumber;

void loop() {
  // put your main code here, to run repeatedly:
  int packetSize = udp.parsePacket();

  if (streamMode == false){
    if (packetSize > 0 ){
      Serial.print("Packet received of size: ");
      Serial.println(packetSize);
      Serial.println("Packet contents: ");
      
      udp.read(packetContentsCMD, 3);
      for (int i = 0; i < sizeof(packetContentsCMD); i++){
        Serial.println(packetContentsCMD[i]);
      }

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

        streamMode = true;
      }
    }
  }
  else{
    //here's where we process packets in stream mode and put them together
    if (packetSize == chunkSize){
      Serial.print("Chunk Number");
      Serial.print(udp.peek());
      Serial.println("Received");
      udp.read(&chunkNumber, 1);
      udp.read(frameBuffer[chunkNumber], chunkSize-1);
      
      udp.beginPacket(deviceIp, PORT);
      udp.write(chunkReceived, 3);
      udp.endPacket();
    }
  }
}
