
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <BlynkSimpleEsp32.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include <esp_int_wdt.h>
#include <esp_task_wdt.h>

#include <time.h>
#define GMT_OFFSET_SEC 3600 * 9
#define DAYLIGHT_OFFSET_SEC 0
#define NTP_SERVER "jp.pool.ntp.org"

#include "secrets.h"
#include "tds_meter.h"

#define relayPumpPin 14

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

#define INFLUXDB_URI "http://192.168.1.113:8086/write?db=hydroponicsController"
#define INFLUXDB_USER "hydroponics"
#define INFLUXDB_PASSWORD "hydroponics"

#define WIFI_TIMEOUT_MS 20000 // 20 second WiFi connection timeout
#define WIFI_RECOVER_TIME_MS 5000 // Wait 5 seconds after a failed connection attempt

#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

#define HOSTNAME "hydroponic-controller"

int waiting = 0;
bool turnOn = true;

void loopWifiKeepAlive(void* pvParameters);
void loopPump(void* pvParameters);

void send_notification(char const* key, char const* value)
{
    HTTPClient http;
    char message[255] = { 0 };
    char url[1024] = { 0 };

    if (WiFi.status() == WL_CONNECTED) {
        http.begin(INFLUXDB_URI);
        http.addHeader("Content-Type", "text/plain");
        http.setAuthorization(INFLUXDB_USER, INFLUXDB_PASSWORD);
        snprintf(url, sizeof(url), "hydroponicsController,device=hydroponicsController %s=%s", key, value);
        http.POST(url);
        http.end();
        snprintf(message, sizeof(message), "Notification sent: %s", value);
    }
}

void OTASetup()
{
    // Port defaults to 3232
    // ArduinoOTA.setPort(3232);

    // Hostname defaults to esp3232-[MAC]
    ArduinoOTA.setHostname(HOSTNAME);

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
            ESP.restart();
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
    Blynk.syncAll();
}

void setupPump(void)
{
    pinMode(relayPumpPin, OUTPUT);
    turnOn = true;
    waiting = 0;
}

void setup(void)
{
    // start serial port
    Serial.begin(9600);

    Serial.printf("Connecting to %s ", ssid);
    setCpuFrequencyMhz(80);

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        Serial.print(".");
    }
    Serial.print("connected");

    OTASetup();
    setupBlynk();
    setupPump();

    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    /* Init watchdog timer */
    esp_task_wdt_init(3, false);

    xTaskCreatePinnedToCore(loopWifiKeepAlive, "loopWifiKeepAlive", 4096, NULL, 3, NULL, ARDUINO_RUNNING_CORE);
    xTaskCreatePinnedToCore(loopPump, "loopPump", 4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(loopTDSMeter, "loopTDSMeter", 4096, NULL, 1, NULL, ARDUINO_RUNNING_CORE);
}

void turnPumpOff()
{
    turnOn = false;
    waiting = 0;
    send_notification("pump", "0");
    Blynk.virtualWrite(V1, LOW);
}

void turnPumpOn()
{
    turnOn = true;
    waiting = 0;
    send_notification("pump", "1");

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


void loopWifiKeepAlive(void* pvParameters)
{
    esp_task_wdt_add(NULL); // Attach task to watchdog
    while (42) {
        esp_task_wdt_reset(); // reset watchdog

        if (WiFi.status() == WL_CONNECTED) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        Serial.println("[WIFI] Connecting");
        // WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, password);

        unsigned long startAttemptTime = millis();

        // Keep looping while we're not connected and haven't reached the timeout
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < WIFI_TIMEOUT_MS) {
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }

        // When we couldn't make a WiFi connection (or the timeout expired)
        // sleep for a while and then retry.
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WIFI] FAILED");
            vTaskDelay(WIFI_RECOVER_TIME_MS / portTICK_PERIOD_MS);
            continue;
        }

        Serial.println("[WIFI] Connected: " + WiFi.localIP());
    }
}

void loopTDSMeter(void* pvParameters)
{
    TDSMeter meter;

    while (42) {
        // If the pump is currently ON wait a bit for the pump to finish before sampling
        while (turnOn == true) {
            Serial.println("Waiting for pump to finish before TDS testing...");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        Serial.println("loopTDSMeter waiting...");
        meter.readTDSValue();

        Blynk.virtualWrite(V0, meter.getTDSValue());
        Blynk.virtualWrite(V2, meter.getTemperature());

        char tdsValueStr[8] = { 0 };
        char temperatureStr[8] = { 0 };

        snprintf(tdsValueStr, sizeof(tdsValueStr), "%f", meter.getTDSValue());
        snprintf(temperatureStr, sizeof(temperatureStr), "%f", meter.getTemperature());

        send_notification("tds", tdsValueStr);
        send_notification("temp", temperatureStr);

        vTaskDelay(5UL * 60UL * 60UL * 1000UL / portTICK_PERIOD_MS); // 5 hours wait
    }
}

void loopPump(void* pvParameters)
{
    struct tm timeinfo;

    turnOn = true;
    waiting = 0;


    while (42) {
        int secToWait = 60 * 30;

        if (getLocalTime(&timeinfo)) {
            /* During the day increase the watering schedule frequency */
            if (timeinfo.tm_hour < 20 && timeinfo.tm_hour > 5) {
                secToWait = 60 * 15;
            }
        }

        if (turnOn == true) {
            if (waiting < 60 * 1) {
                digitalWrite(relayPumpPin, HIGH);
            } else {
                waiting = 0;
                turnPumpOff();
                //esp_deep_sleep(15 * 60 * 1000 * 1000); // 15 minutes
            }
        }

        if (turnOn == false) {
            if (waiting < secToWait) {
                digitalWrite(relayPumpPin, LOW);
            } else {
                waiting = 0;
                turnPumpOn();
            }
        }

        waiting++;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void loop(void)
{
    Blynk.run();
    ArduinoOTA.handle();
}
