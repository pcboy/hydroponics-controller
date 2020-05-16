
#include <Arduino.h>

// Temperature sensor
#include <DallasTemperature.h>
#include <OneWire.h>
#define ONE_WIRE_BUS 13
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

#define TdsSensorPin 32

#define TdsRelayPin 33
#define VREF 3.3 // analog reference voltage(Volt) of the ADC
#define SCOUNT 255 // sum of sample point
int analogBuffer[SCOUNT]; // store the analog value in the array, read from ADC
int analogBufferTemp[SCOUNT];
int analogBufferIndex = 0, copyIndex = 0;
float averageVoltage = 0;

#include "tds_meter.h"

TDSMeter::TDSMeter()
{
    pinMode(TdsSensorPin, INPUT);
    pinMode(TdsRelayPin, OUTPUT);
}

float TDSMeter::readTemperature()
{
    delay(1000);
    sensors.begin();
    delay(1000);
    sensors.requestTemperatures();
    temperature = sensors.getTempCByIndex(0);
    return temperature;
}

float TDSMeter::readTDSValue()
{
    static unsigned long analogSampleTimepoint = millis();

    turnOnTDS();
    analogBufferIndex = 0;

    while (42) {
        if (millis() - analogSampleTimepoint > 40U) // every 40 milliseconds,read the analog value from the ADC
        {
            analogSampleTimepoint = millis();
            analogBuffer[analogBufferIndex] = analogRead(TdsSensorPin); //read the analog value and store into the buffer
            analogBufferIndex++;
            // we have enough values
            if (analogBufferIndex == SCOUNT) {
                for (copyIndex = 0; copyIndex < SCOUNT; copyIndex++)
                    analogBufferTemp[copyIndex] = analogBuffer[copyIndex];

                float temperature = readTemperature();
                averageVoltage = getMedianNum(analogBufferTemp, SCOUNT) * (float)VREF / 1024.0; // read the analog value more stable by the median filtering algorithm, and convert to voltage value
                float compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0); //temperature compensation formula: fFinalResult(25^C) = fFinalResult(current)/(1.0+0.02*(fTP-25.0));
                float compensationVoltage = averageVoltage / compensationCoefficient; //temperature compensation
                tdsValue = (133.42 * compensationVoltage * compensationVoltage * compensationVoltage - 255.86 * compensationVoltage * compensationVoltage + 857.39 * compensationVoltage) * 0.5; //convert voltage value to tds value
                //Serial.print("voltage:");
                //Serial.print(averageVoltage,2);
                //Serial.print("V ");
                Serial.print("TDS Value:");
                Serial.print(tdsValue, 0);
                Serial.println("ppm");
                turnOffTDS();

                return tdsValue;
            }
        }
    }

    return 0.0f;
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

void TDSMeter::turnOnTDS()
{
    Serial.println("TURN ON TDS");
    digitalWrite(TdsRelayPin, HIGH);
}

void TDSMeter::turnOffTDS()
{
    Serial.println("TURN OFF TDS");
    digitalWrite(TdsRelayPin, LOW);
}
