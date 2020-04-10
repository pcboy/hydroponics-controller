
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <BlynkSimpleEsp32.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "secrets.h"
#include "tds_meter.h"

#define relayPumpPin 14

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
#define INFLUXDB_URI "http://192.168.1.113:8086/write?db=hydroponicsController"
#define INFLUXDB_USER "hydroponics"
#define INFLUXDB_PASSWORD "hydroponics"

#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

void loopPump(void* pvParameters);

void send_notification(int value)
{
    if (value < 0)
        return;

    HTTPClient http;
    http.begin(INFLUXDB_URI);
    http.addHeader("Content-Type", "text/plain");
    http.setAuthorization(INFLUXDB_USER, INFLUXDB_PASSWORD);
    http.POST("hydroponicsController,device=hydroponicsController temp=" + String(value));
    http.end();
    Serial.println("Notification sent: " + value);
}

void OTASetup()
{
    // Port defaults to 3232
    // ArduinoOTA.setPort(3232);

    // Hostname defaults to esp3232-[MAC]
    ArduinoOTA.setHostname("hydroponic-controller");

    // No authentication by default
    // ArduinoOTA.setPassword("admin");

    // Password can be set with it's md5 value as well
    // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
    // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

    ArduinoOTA
        .onStart([]() {
            String type;
            if (ArduinoOTA.getCommand() == U_FLASH)
                type = "sketch";
            else // U_SPIFFS
                type = "filesystem";

            // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
            Serial.println("Start updating " + type);
        })
        .onEnd([]() {
            Serial.println("\nEnd");
        })
        .onProgress([](unsigned int progress, unsigned int total) {
            Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
        })
        .onError([](ota_error_t error) {
            Serial.printf("Error[%u]: ", error);
            if (error == OTA_AUTH_ERROR)
                Serial.println("Auth Failed");
            else if (error == OTA_BEGIN_ERROR)
                Serial.println("Begin Failed");
            else if (error == OTA_CONNECT_ERROR)
                Serial.println("Connect Failed");
            else if (error == OTA_RECEIVE_ERROR)
                Serial.println("Receive Failed");
            else if (error == OTA_END_ERROR)
                Serial.println("End Failed");
        });

    ArduinoOTA.begin();
}

void setupBlynk()
{
    char auth[] = BLYNK_TOKEN;
    Blynk.begin(auth, ssid, password);
}

BLYNK_CONNECTED()
{
    OTASetup();
    Blynk.syncAll();
}

void setupPump(void)
{
    pinMode(relayPumpPin, OUTPUT);
}

void setup(void)
{
    // start serial port
    Serial.begin(9600);

    Serial.printf("Connecting to %s ", ssid);
    setCpuFrequencyMhz(80);

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.print("connected");

    setupBlynk();
    setupPump();
    setupTDSMeter();

    xTaskCreatePinnedToCore(loopTDSMeter, "loopTDSMeter", 4096, NULL, 1, NULL, ARDUINO_RUNNING_CORE);
    xTaskCreatePinnedToCore(loopPump, "loopPump", 4096, NULL, 1, NULL, ARDUINO_RUNNING_CORE);
}

int waiting = 0;
bool turnOn = true;

void turnPumpOff()
{
    turnOn = false;
    waiting = 0;
    Blynk.virtualWrite(V1, LOW);
}

void turnPumpOn()
{
    turnOn = true;
    waiting = 0;
    Blynk.virtualWrite(V1, HIGH);
}

BLYNK_WRITE(V1) // Button to turn the pump on/off
{
    int pinData = param.asInt();

    if (pinData == 1) {
        turnPumpOn();
    } else {
        turnPumpOff();
    }
}

void loopTDSMeter(void* pvParameters)
{
    while (42) {
        float tdsVal = 0;

        Serial.println("loopTDSMeter waiting...");
        tdsVal = getTDSValue();
        Blynk.virtualWrite(V0, tdsVal);
        delay(5UL * 60UL * 60UL * 1000UL); // 5 hours wait

        // If the pump is currently ON wait a bit for the pump to finish before sampling
        while (turnOn == true) {
            Serial.println("Waiting for pump to finish before TDS testing...");
            delay(1000);
        }
    }
}

void loopPump(void* pvParameters)
{
    while (42) {
        if (turnOn == true) {
            if (waiting < 60) {
                digitalWrite(relayPumpPin, HIGH);
            } else {
                turnPumpOff();
                //send_notification(turnOn);
            }
        }

        if (turnOn == false) {
            if (waiting < 60 * 15) {
                digitalWrite(relayPumpPin, LOW);
            } else {
                turnPumpOn();
                //send_notification(turnOn);
            }
        }

        waiting++;
        delay(1000);
    }
}

void loop(void)
{
    Blynk.run();
    ArduinoOTA.handle();
}
