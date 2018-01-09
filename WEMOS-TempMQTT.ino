#include "config.h"

#include <DHT.h>
#include <DHT_U.h>
#include <Adafruit_Sensor.h>
#include <ESP8266WiFi.h>
#include <MQTTClient.h>          // MQTT Lib: https://github.com/256dpi/arduino-mqtt
#include <ArduinoOTA.h>
#include <FS.h>

// ToDo: OTA and allow topic setting via MQTT to MAC

// Define Pin for Sensor - I use GPIO0, wich is D3 on WEMOS
#define DHTPIN 0
#define DHTTYPE DHT22   // DHT 22  (AM2302)

// init DHT; 3rd parameter = 16 works for ESP8266@80MHz
// FixMe: What is the third parameter??
DHT dht(DHTPIN, DHTTYPE, 16); 

WiFiClient wifiClient;
MQTTClient mqttClient;

// Enable ADC to measure Vcc
ADC_MODE(ADC_VCC);

// Generally, you should use "unsigned long" for variables that hold time
unsigned long previousMillisDHT  = 0;        // will store last temp was read
unsigned long previousMillisMQTT = 0;

// Char buffer for converting floats to strings
char buffer[20];

// String for storing WiFi MAC adress
String mac;

// String for storing the MQTT (parent) topic to send values to
String mqttTopic(mqttUserTopic);

float hum, temp;  // Values read from sensor
int vcc;          // Vcc measured by internal ADC; unit: 1/1024 Volt

void messageReceivedMQTT(String &topic, String &payload) {
  Serial.println("MQTT incoming topic: " + topic + " - Value: " + payload);
  
  if(topic.indexOf("location") > 0) {
    mqttTopic=payload;
    
    File configFile = SPIFFS.open("/mqtt_location.txt", "w");
    if (!configFile) { 
      Serial.println("Failed to open mqtt_location.txt for writing new location!");
    } else {
      Serial.print("Saving new MQTT location to mqtt_location.txt: "); Serial.println(payload);
      configFile.println(payload);
      configFile.close();
    }
  }
}

void getTempHum() {
  unsigned long currentMillis = millis();

  // Make sure we only read new values if enough time has passed
  while(currentMillis - previousMillisDHT < dhtInterval) {
    currentMillis = millis();
  }
  
  // Reading temperature for humidity takes about 250 milliseconds!
  do {
    hum  = dht.readHumidity();        // Read humidity (percent)
    temp = dht.readTemperature(false);// Read temperature as Celsius ...(true)=Fahrenheit
  } while(isnan(hum) || isnan(temp)); // Check if any reads failed and try again

  // After reading new values we store the time for the next round
  previousMillisDHT = millis();  
}

void connectWiFi() {
  String hostname(HOSTNAME);

  Serial.println("--- WiFi Setup Start ---");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.printDiag(Serial);

  mac = WiFi.macAddress(); 
  mac.replace(":", "");
  Serial.print("MAC: "); Serial.println(mac);
  
  hostname += mac;
  Serial.print("\nSetting Hostname to: "); Serial.println(hostname);
  WiFi.hostname(hostname);
  
  WiFi.begin(wifi_ssid, wifi_pass);
  Serial.print("Connecting wifi...");

  while (WiFi.status() != WL_CONNECTED) {
     Serial.print(".");
     delay(250);
  }
  Serial.print("\nWiFi connected! IP address: "); Serial.println(WiFi.localIP());
  Serial.println("--- WiFi Setup End   ---");
}

void connectMQTT() {
  // Note: Local domain names (e.g. "Computer.local" on OSX) are not supported by Arduino.
  // You need to set the IP address directly.
  String mqttID = String("ESP_"+mac);
  
  Serial.println("\n--- MQTT Setup Start ---");
  Serial.println("\nconnecting mqtt...");
  
  mqttClient.begin(mqttServer, wifiClient);
  mqttClient.onMessage(messageReceivedMQTT);
  Serial.print("Connection to MQTT server with client ID: "); Serial.println(mqttID.c_str());
  while (!mqttClient.connect(mqttID.c_str())) {
     Serial.print(".");
     delay(250);
   }
   Serial.println("\nMQTT connected!");
   Serial.println("\n--- MQTT Setup End   ---");
}

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n\nStarting up\n============\n");
  Serial.print("\nChip ID: 0x");  Serial.println(ESP.getChipId(), HEX);
 
  connectWiFi();
  connectMQTT();

  Serial.println("\n\n--- Filesystem and MQTT-Sub Start ---");
  // Setup filesystem
  if(!SPIFFS.begin()) { Serial.println("\n!! Error during setup Filesystem! Will use default values for sending MQTT Topic"); }

  if(mqttTopic.length()==0) {
    Serial.println("No MQTT User Topic existing. Trying to load from config, fallback to MAC Address");
    mqttTopic = String(mac);
    
    File configFile = SPIFFS.open("/mqtt_location.txt", "r");
    if (!configFile) { Serial.println("Failed to open mqtt_location.txt. Using MAC as MQTT topic."); }
    else { 
       mqttTopic = configFile.readString();
       Serial.print("MQTT Topic read from mqtt_location.txt: "); Serial.println(mqttTopic);
       configFile.close();
       mqttTopic.trim();
    }
  } else { 
    Serial.println("MQTT User Topic provided during compile time. Overriding other values.");
  }
  Serial.println("--- MQTT Sub/Pub Summary ---");
  Serial.print("MQTT publish base topic for this device: "); Serial.println(mqttTopic);
  Serial.print("MQTT subscription topic for this device: "); Serial.println(mac+"/#");
  mqttClient.subscribe(mac + "/#");
  Serial.println("\n--- Filesystem and MQTT-Sub End   ---");
  
  // Start OTA server.
  ArduinoOTA.setHostname((const char *)((WiFi.hostname()).c_str()));
  ArduinoOTA.begin();

  Serial.println("\n\nEND OF SETUP - Start of normal operation\n========================================\n\n");
}

void loop() {
  // First some handles to call for background tasks
  mqttClient.loop();
  ArduinoOTA.handle();

  // publish a message roughly after "measureDelay" ms
  if (millis() - previousMillisMQTT > measureDelay) {
     
    previousMillisMQTT = millis();
    getTempHum();
    vcc=ESP.getVcc();

    // Check if we are still connected to the MQTT server. If not: reconnect
    if (!mqttClient.connected()) {                // Problem with MQTT server
      Serial.println("\nConnection to MQTT broker lost. Reconnecting.\n");
      
      if (WiFi.status() != WL_CONNECTED) connectWiFi();
      connectMQTT();
      Serial.println("Connection to MQTT restored.");
    }

    Serial.println("\nSending values via MQTT.");
    
    dtostrf(temp, 5, 1, buffer); mqttClient.publish((mqttTopic+"/temp").c_str(), buffer);
    dtostrf( hum, 5, 1, buffer); mqttClient.publish((mqttTopic+"/hum").c_str() , buffer);
    sprintf(buffer, "%i", vcc);  mqttClient.publish((mqttTopic+"/vcc").c_str() , buffer);
    
    Serial.print("temp: "); Serial.println(temp); 
    Serial.print("hum: ");  Serial.println(hum);
    Serial.print("vcc: ");  Serial.println(vcc); 
  }
}



