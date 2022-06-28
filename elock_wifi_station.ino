#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <SoftwareSerial.h>

#define AP_SSID "E-Lock AP"
#define AP_PASS "vitomatic"
#define ARD_RX 12
#define ARD_TX 14
#define BUFF_SIZE 15

IPAddress mqttServer(159, 223, 235, 208);
SoftwareSerial ard_serial;

void callback(char* topic, byte* payload, unsigned int length) {
  ard_serial.write(payload, length);
  ard_serial.flush();
}

WiFiManager wm;
WiFiClient espClient;
PubSubClient pubSubClient(mqttServer, 1883, callback, espClient);

void sendToApp(){
  if (ard_serial.available() > 6){
    byte buff[BUFF_SIZE];
    uint8_t bytes_amount = ard_serial.readBytes(buff, BUFF_SIZE);
    pubSubClient.publish("elock_status", buff, bytes_amount);
  }
}

void reconnect() {
  if (WiFi.status() == WL_CONNECTED){
    while (!pubSubClient.connected()) {
      Serial.print("Attempting MQTT connection...");
      if (pubSubClient.connect("ESP8266Client")) {
        Serial.println("connected");
        pubSubClient.subscribe("elock_commands");
      } else {
        Serial.print("failed, rc=");
        Serial.print(pubSubClient.state());
        Serial.println(" . Trying again");
      }
    }
  }
  else
    Serial.println("WiFi is not connected");
}

void setup() {
  Serial.begin(115200);
  ard_serial.begin(19200, SWSERIAL_8N1, ARD_RX, ARD_TX, false, 64);
  WiFi.mode(WIFI_STA);
  Serial.println();
  Serial.println("Connecting to WiFi or starting AP");
  if (wm.autoConnect(AP_SSID, AP_PASS) && WiFi.status() == WL_CONNECTED)
    Serial.println("Connected to WiFi");
  else
    Serial.println("Failed to establish connection or AP");
}

void loop() {
  if (!pubSubClient.connected())
    reconnect();
  pubSubClient.loop();
  sendToApp();
}
