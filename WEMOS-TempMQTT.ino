#include "config.h"
#include <DHT_U.h>
#include <Adafruit_Sensor.h>
#include <ESP8266WiFi.h>
#include <MQTTClient.h>    // MQTT Lib: https://github.com/256dpi/arduino-mqtt
//#include <FS.h>

// Define Pin for Sensor - I use GPIO2, wich is D4 on WEMOS
#define DHTPIN 2

// Uncomment whatever type you're using!
//#define DHTTYPE DHT11     // DHT 11 
#define DHTTYPE DHT22   // DHT 22  (AM2302)
//#define DHTTYPE DHT21   // DHT 21 (AM2301)

// init DHT; 3rd parameter = 16 works for ESP8266@80MHz
// FixMe: What is the third parameter??
DHT dht(DHTPIN, DHTTYPE, 16); 

WiFiClient wifiClient;
MQTTClient mqttClient;

// Enable ADC to measure Vcc
ADC_MODE(ADC_VCC);

int lastMillis;
// Generally, you should use "unsigned long" for variables that hold time
const long interval = 2000;              // interval at which to read sensor
unsigned long previousMillis = 0;        // will store last temp was read

char buffer[20];

float hum, temp;  // Values read from sensor
int vcc;    // Vcc measured by internal ADC; unit should be 1/1024 Volt

void messageReceived(String &topic, String &payload) {
  Serial.println("incoming: " + topic + " - " + payload);
}

void gettemperature() {
  // Wait at least 2 seconds seconds between measurements.
  // if the difference between the current time and last time you read
  // the sensor is bigger than the interval you set, read the sensor
  // Works better than delay for things happening elsewhere also
  unsigned long currentMillis = millis();
 
  if(currentMillis - previousMillis >= interval) {
    // save the last time you read the sensor 
    previousMillis = currentMillis;   
 
    // Reading temperature for humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
    hum  = dht.readHumidity();        // Read humidity (percent)
    temp = dht.readTemperature();     // Read temperature as Celsius ...(true)=Fahrenheit
    // Check if any reads failed and exit early (to try again).
    if (isnan(hum) || isnan(temp)) {
      return;
    }
  }
}

void setup() {

    // Short delay after boot... seems needed sometimes
    for(uint8_t t = 4; t > 0; t--) {
        delay(1000);
    }

  Serial.begin(115200);
  
  Serial.println("\n\nStarting wifi\n");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting wifi...");
  
  while (WiFi.status() != WL_CONNECTED) {
     Serial.print(".");
     delay(1000);
  }
  Serial.print("WiFi connected! IP address: "); Serial.println(WiFi.localIP());


  Serial.print("\nconnecting mqtt...");
 
  // Note: Local domain names (e.g. "Computer.local" on OSX) are not supported by Arduino.
  // You need to set the IP address directly.
  mqttClient.begin(mqttServer, wifiClient);
  mqttClient.onMessage(messageReceived);

  while (!mqttClient.connect("arduino", "try", "try")) {
     Serial.print(".");
     delay(1000);
   }

  Serial.println("\nconnected!");

  // mqttClient.subscribe("/hello");
  // client.unsubscribe("/hello");
}

void loop() {
  // put your main code here, to run repeatedly:
  mqttClient.loop();

  if (!mqttClient.connected()) {
      // Probelm with mqtt
      Serial.println("\nConnection to MQTT broker lost.");
  }

  gettemperature();
  vcc=ESP.getVcc();

  // publish a message roughly every five seconds.
  if (millis() - lastMillis > 5000) {
    lastMillis = millis();
    Serial.println("Sending values via MQTT!");
    dtostrf(temp, 5, 1, buffer); mqttClient.publish("nordzimmer/temp", buffer);
    dtostrf( hum, 5, 1, buffer); mqttClient.publish("nordzimmer/hum" , buffer);
    sprintf(buffer, "%i", vcc);  mqttClient.publish("nordzimmer/vcc" , buffer);
    Serial.println(temp); Serial.println(buffer); //Serial.println(hum); Serial.println(vcc); 
  }
}



