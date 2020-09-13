/*
   RadioLib SX128x Transmit Example

   This example transmits packets using SX1280 LoRa radio module.
   Each packet contains up to 256 bytes of data, in the form of:
    - Arduino String
    - null-terminated char array (C-string)
    - arbitrary binary data (byte array)

   Other modules from SX128x family can also be used.

   For full API reference, see the GitHub Pages
   https://jgromes.github.io/RadioLib/
*/
 #define DEBUG 1

#define POWER_SENSORS 27
#define BUILTIN_LED 22
#define BATTERYPIN 36

// include the library

#define MINUTES_BETWEEN 30
unsigned long entryTCS;

#include <Adafruit_VEML6070.h>
#include <Adafruit_TCS34725.h>
#include <Adafruit_TSL2561_U.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <credentials.h>
#include "MQTT.h"


#define TCS34725

// some magic numbers for this device from the DN40 application note
#define TCS34725_R_Coef 0.136
#define TCS34725_G_Coef 1.000
#define TCS34725_B_Coef -0.444
#define TCS34725_GA 1.0
#define TCS34725_DF 310.0
#define TCS34725_CT_Coef 3810.0
#define TCS34725_CT_Offset 1391.0


String JSONmessage;
int intensity;
int uvValue;
uint16_t r, g, b, c, colorTemp, lux;
float voltage;

Adafruit_VEML6070 uv = Adafruit_VEML6070();
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_700MS, TCS34725_GAIN_1X);
Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_FLOAT, 12345);

void configureTSL2561(void)
{
#ifdef DEBUG
  Serial.print("Init TSL2561...");
#endif
  /* You can also manually set the gain or enable auto-gain support */
  // tsl.setGain(TSL2561_GAIN_1X);      /* No gain ... use in bright light to avoid sensor saturation */
  // tsl.setGain(TSL2561_GAIN_16X);     /* 16x gain ... use in low light to boost sensitivity */
  tsl.enableAutoRange(true);            /* Auto-gain ... switches automatically between 1x and 16x */

  /* Changing the integration time gives you better sensor resolution (402ms = 16-bit data) */
  // tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_13MS);      /* fast but low resolution */
  //tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_101MS);  /* medium resolution and speed   */
  tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_402MS);  /* 16-bit data but slowest conversions */

  /* Update these values depending on what you've set above! */

#ifdef DEBUG
  Serial.println("------------------------------------");
  Serial.print  ("Gain:         "); Serial.println("Auto");
  Serial.print  ("Timing:       "); Serial.println("13 ms");
  Serial.println("------------------------------------");

  Serial.println("success");
#endif
}

void displaySensorDetails(void)
{
  sensor_t sensor;
  tsl.getSensor(&sensor);
#ifdef DEBUG
  Serial.println("------------------------------------");
  Serial.print  ("Sensor:       "); Serial.println(sensor.name);
  Serial.print  ("Driver Ver:   "); Serial.println(sensor.version);
  Serial.print  ("Unique ID:    "); Serial.println(sensor.sensor_id);
  Serial.print  ("Max Value:    "); Serial.print(sensor.max_value); Serial.println(" lux");
  Serial.print  ("Min Value:    "); Serial.print(sensor.min_value); Serial.println(" lux");
  Serial.print  ("Resolution:   "); Serial.print(sensor.resolution); Serial.println(" lux");
  Serial.println("------------------------------------");
  Serial.println("");
  delay(500);
#endif
}

void readSensors() {
  digitalWrite(POWER_SENSORS, HIGH);

  voltage = readVoltage(50);
  Serial.print("Batt ");
  Serial.println(voltage);
  // Read VEML6070
  uvValue = uv.readUV();
  if (uvValue < 0) {
    Serial.print("read UV sensor failed!!");
  }
#ifdef DEBUG
  Serial.print("UV= ");
  Serial.println(uvValue);
#endif

  //TSL2561
  sensors_event_t event;
  tsl.getEvent(&event);
  if (event.light) {
    intensity = event.light;
#ifdef DEBUG
    Serial.print("Intensity= "); Serial.println(intensity);
#endif
  } else {
    // If event.light = 0 lux the sensor is probably saturated and no reliable data could be generated!
    Serial.println("TSL2561 overload");
    intensity = -1;
  }
  tcs.getRawData(&r, &g, &b, &c);
  // colorTemp = tcs.calculateColorTemperature(r, g, b);
  colorTemp = tcs.calculateColorTemperature_dn40(r, g, b, c);
  lux = tcs.calculateLux(r, g, b);
#ifdef DEBUG
  Serial.print("Color Temp: "); Serial.print(colorTemp, DEC); Serial.print(" K - ");
  Serial.print("Lux: "); Serial.print(lux, DEC); Serial.print(" - ");
  Serial.print("R: "); Serial.print(r, DEC); Serial.print(" ");
  Serial.print("G: "); Serial.print(g, DEC); Serial.print(" ");
  Serial.print("B: "); Serial.print(b, DEC); Serial.print(" ");
  Serial.print("C: "); Serial.print(c, DEC); Serial.print(" ");
  Serial.println(" ");
#endif
  digitalWrite(POWER_SENSORS, LOW);
}

void transmitData() {
  char msg[200];
  StaticJsonDocument<200> doc;
#ifdef DEBUG
  Serial.println(F("[SX1280] Transmitting packet ... "));
#endif

  // you can transmit C-string or Arduino string up to
  // 256 characters long
  // NOTE: transmit() is a blocking method!
  //       See example SX128x_Transmit_Interrupt for details
  //       on non-blocking transmission method.


  doc["L"] = intensity;
  doc["UV"] = uvValue;
#ifdef TCS34725
  doc["LA"] = lux;
  doc["R"] = r;
  doc["G"] = g;
  doc["B"] = b;
  doc["IR"] = (r + g + b > c) ? (r + g + b - c) / 2 : 0;
  doc["CT"] = colorTemp;

#else
  doc["LA"] = 0;
  doc["R"] = 0;
  doc["G"] = 0;
  doc["B"] = 0;
#endif

  doc["Bat"] = voltage;

  JSONmessage = "";
  serializeJson(doc, JSONmessage);
  JSONmessage.toCharArray(msg, JSONmessage.length() + 1);
#ifdef DEBUG
  Serial.print("Publish message: ");
  Serial.println(msg);
  Serial.println(JSONmessage);
  Serial.println(JSONmessage.length());
  delay(1000);
#endif
  client.publish("LIGHTSENSOR/values", msg);
}

float readVoltage(int samples) {
  long volt = 0;
  for (int i = 0; i < samples; i++) {
    volt = volt + analogRead(BATTERYPIN);
  }

  return (volt / samples *0.00111);
}

//----------------------------------------------

void setup() {

  Serial.begin(115200);
  Serial.println("Connected_Light_Meter_Transmitter_ESP32V1");
  pinMode(POWER_SENSORS, OUTPUT);
  digitalWrite(POWER_SENSORS, HIGH);
  // pinMode(0, INPUT);
  // pinMode(2, INPUT);

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  Serial.println();
  Serial.print("Init VEML6070 ");
  uv.begin(VEML6070_1_T); // pass in the integration time constant) {
  if (uv.readUV() == 0xFFFF) {
    Serial.println("Failed");
  } else {
    Serial.println("Success");
  }

#ifdef TCS34725
  // TCS34725 init
  Serial.print("Init TCS34725 ");
  if (tcs.begin()) {
    Serial.println("Success");
  } else {
    Serial.println("Failed");
  }
#endif
  Serial.print("Init TSL2561 ");
  if (!tsl.begin()) {
    Serial.println("Failed");
  } else {
    Serial.println("Success");
  }
  Serial.println();
  configureTSL2561();
  displaySensorDetails();

  Serial.println("Init done");

  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  readSensors();
  transmitData();
  Serial.println("Going to sleep");
  delay(400);
  ESP.deepSleep(MINUTES_BETWEEN * 30e6);
}

void loop() {

  yield();
}
