#pragma once

class TDSMeter {
private:
    float tdsValue;
    float temperature;

public:
    TDSMeter();
    float readTemperature();
    float getTemperature() { return this->temperature; };
    float readTDSValue();
    float getTDSValue() { return this->tdsValue; };

private:
    int getMedianNum(int bArray[], int iFilterLen);
    float readTdsSensor(int numSamples, float sampleDelay);
    float convertToPPM(float analogReading);
    void turnOnTDS();
    void turnOffTDS();
};

void loopTDSMeter(void* pvParameters);