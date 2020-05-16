#pragma once

class TDSMeter {
private:
    float tdsValue;
    float temperature;

public:
    TDSMeter();
    float readTemperature();
    float getTemperature() { return temperature; };
    float readTDSValue();
    float getTDSValue() { return tdsValue; };

private:
    int getMedianNum(int bArray[], int iFilterLen);
    void turnOnTDS();
    void turnOffTDS();
};

void loopTDSMeter(void* pvParameters);