
#include <Arduino.h>

// Temperature sensor
#include <DallasTemperature.h>
#include <OneWire.h>
#define ONE_WIRE_BUS 13
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

#define TDS_ANALOG_GPIO ADC1_CHANNEL_0

#define TDS_ENABLE_GPIO 32

#define TDS_NUM_SAMPLES 20 //(int) Number of reading to take for an average
#define TDS_SAMPLE_PERIOD 2 //(int) Sample period (delay between samples == sample period / number of readings)
#define TDS_VREF 1.1 //(float) Voltage reference for ADC. We should measure the actual value of each ESP32

#define SAMPLE_DELAY ((TDS_SAMPLE_PERIOD / TDS_NUM_SAMPLES) * 1000)

#include "tds_meter.h"
#include <driver/adc.h>

TDSMeter::TDSMeter()
{
    pinMode(TDS_ANALOG_GPIO, INPUT);
    pinMode(TDS_ENABLE_GPIO, OUTPUT);

    adc2_vref_to_gpio(GPIO_NUM_25); // expose VREF to gpio 25
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(TDS_ANALOG_GPIO, ADC_ATTEN_DB_11);
    this->temperature = 0;
    this->tdsValue = 0;
}

float TDSMeter::readTemperature()
{
    sensors.begin();
    sensors.requestTemperatures();
    this->temperature = sensors.getTempCByIndex(0);

    return this->temperature;
}

float TDSMeter::convertToPPM(float analogReading)
{
    float adcCompensation = 1 + (1 / 3.9); // 1/3.9 (11dB) attenuation.
    float vPerDiv = (TDS_VREF / 4096) * adcCompensation; // Calculate the volts per division using the VREF taking account of the chosen attenuation value.
    float averageVoltage = analogReading * vPerDiv; // Convert the ADC reading into volts
    float compensationCoefficient = 1.0 + 0.02 * (this->temperature - 25.0); //temperature compensation formula: fFinalResult(25^C) = fFinalResult(current)/(1.0+0.02*(fTP-25.0));
    float compensationVoltage = averageVoltage / compensationCoefficient; //temperature compensation
    float tdsValue = (133.42 * compensationVoltage * compensationVoltage * compensationVoltage - 255.86 * compensationVoltage * compensationVoltage + 857.39 * compensationVoltage) * 0.5; //convert voltage value to tds value

    ESP_LOGI(TDS, "Volts per division = %f", vPerDiv);
    ESP_LOGI(TDS, "Average Voltage = %f", averageVoltage);
    ESP_LOGI(TDS, "Temperature (currently fixed, we should measure this) = %f", this->temperature);
    ESP_LOGI(TDS, "Compensation Coefficient = %f", compensationCoefficient);
    ESP_LOGI(TDS, "Compensation Voltge = %f", compensationVoltage);
    ESP_LOGI(TDS, "tdsValue = %f ppm", tdsValue);
    return tdsValue;
}

int TDSMeter::getMedianNum(int bArray[], int iFilterLen)
{
    int bTab[iFilterLen];
    for (byte i = 0; i < iFilterLen; i++)
        bTab[i] = bArray[i];

    int i, j, bTemp;

    for (j = 0; j < iFilterLen - 1; j++) {
        for (i = 0; i < iFilterLen - j - 1; i++) {
            if (bTab[i] > bTab[i + 1]) {
                bTemp = bTab[i];
                bTab[i] = bTab[i + 1];
                bTab[i + 1] = bTemp;
            }
        }
    }
    if ((iFilterLen & 1) > 0)
        bTemp = bTab[(iFilterLen - 1) / 2];
    else
        bTemp = (bTab[iFilterLen / 2] + bTab[iFilterLen / 2 - 1]) / 2;
    return bTemp;
}

float TDSMeter::readTdsSensor(int numSamples, float sampleDelay)
{
    // Take n sensor readings every p millseconds where n is numSamples, and p is sampleDelay.
    // Return the average sample value.
    int bArray[numSamples] = { 0 };

    for (int i = 0; i < numSamples; i++) {
        // Read analogue value
        int analogSample = adc1_get_raw(TDS_ANALOG_GPIO);
        ESP_LOGI(TDS, "Read analog value %d then sleep for %f milli seconds.", analogSample, sampleDelay);
        bArray[i] = analogSample;
        vTaskDelay(sampleDelay / portTICK_PERIOD_MS);
    }

    float tdsMedian = getMedianNum(bArray, numSamples);
    ESP_LOGI(TDS, "Calculated median = %f", tdsMedian);
    return tdsMedian;
}

float TDSMeter::readTDSValue()
{
    //turnOnTDS();


    this->readTemperature();
    //float sensorReading = readTdsSensor(TDS_NUM_SAMPLES, SAMPLE_DELAY);
    //this->tdsValue = convertToPPM(sensorReading);
    this->tdsValue = 0;
    //turnOffTDS();
    return this->tdsValue;
}

void TDSMeter::turnOnTDS()
{
    Serial.println("TURN ON TDS");
    digitalWrite(TDS_ENABLE_GPIO, HIGH);
    vTaskDelay(10000 / portTICK_PERIOD_MS);
}

void TDSMeter::turnOffTDS()
{
    Serial.println("TURN OFF TDS");
    digitalWrite(TDS_ENABLE_GPIO, LOW);
}
