// Debug del monitor seriale: 0 = produzione, 1 = debug a 115200 baud.
// Stesso schema di FireAllarmRobot. Attivo per diagnosticare gli sganci.
#ifndef DEBUG_SERIAL
#define DEBUG_SERIAL 1
#endif

#if DEBUG_SERIAL
#define DEBUG_SERIAL_BEGIN(...) do { Serial.begin(__VA_ARGS__); } while (0)
#define DEBUG_SERIAL_PRINT(...) do { Serial.print(__VA_ARGS__); } while (0)
#define DEBUG_SERIAL_PRINTLN(...) do { Serial.println(__VA_ARGS__); } while (0)
#else
#define DEBUG_SERIAL_BEGIN(...) do { } while (0)
#define DEBUG_SERIAL_PRINT(...) do { } while (0)
#define DEBUG_SERIAL_PRINTLN(...) do { } while (0)
#endif

// Radio RF: 0 = disattivata, 1 = percorso wireless originale.
// Per questa diagnosi resta a 0, indipendentemente dal debug seriale.
#ifndef RF_ENABLED
#define RF_ENABLED 0
#endif

#include <Arduino.h>
#include <math.h>
#if RF_ENABLED
#include <VirtualWire.h>
#include <RFWirelessTransmitter.h>
#endif

// Valori dei partitori originali: non sono quelli del nuovo documento Word.
int COMMONRESISTOR = 22000;
int R1 = 10100;
int R2 = 3988;
int R3 = 2458;
double R4 = 9928;
int R5 = 6763;
int R6 = 12100;

const uint8_t BATTERY_COUNT = 6;
const uint8_t RELAY_PIN = 5;
const uint8_t ANALOG_PORTS[BATTERY_COUNT] = { A0, A1, A2, A3, A4, A5 };
const uint8_t SAMPLES_PER_CHANNEL = 16;
const float VOLTAGE_REFERENCE = 5.00f;

// Soglie originali: il minimo e' ammesso, il massimo fa scattare l'allarme.
const float ADC_MIN_VOLTAGE = 2.00f;
const float ADC_MAX_VOLTAGE = 4.80f;
const float BATTERY_MIN_VOLTAGE = 11.00f;
const float BATTERY_MAX_VOLTAGE = 14.50f;
const unsigned long READING_INTERVAL_MS = 250UL;
const unsigned long ALARM_CONFIRM_TIME_MS = 500UL;
const uint8_t REQUIRED_ALARM_READINGS = 3;
const unsigned long DEBUG_INTERVAL_MS = 1000UL;

const uint8_t ALARM_ADC_LOW = 1;
const uint8_t ALARM_ADC_HIGH = 2;
const uint8_t ALARM_BATTERY_LOW = 4;
const uint8_t ALARM_BATTERY_HIGH = 8;
const uint8_t ALARM_INVALID_READING = 16;

float adcVoltageValues[BATTERY_COUNT] = {};
float batteryVoltageValues[BATTERY_COUNT] = {};
float singleBatteryVoltageValue[BATTERY_COUNT] = {};
uint8_t batteryAlarmFlags[BATTERY_COUNT] = {};
uint8_t alarmReadingCount[BATTERY_COUNT] = {};
unsigned long alarmStartMs[BATTERY_COUNT] = {};

bool hasMeasurements = false;
bool relayTripped = false;
unsigned long lastReadingStartMs = 0;
unsigned long measurementTimeMs = 0;

// Conserva il primo sgancio fino al riavvio, anche se le tensioni rientrano.
uint8_t tripChannel = 0;
uint8_t tripAlarmFlags = 0;
float tripAdcVoltage = 0.0f;
float tripBatteryVoltage = 0.0f;
unsigned long tripTimeMs = 0;

#if DEBUG_SERIAL
unsigned long lastDebugMs = 0;
bool hasPrintedMeasurements = false;
#endif

#if RF_ENABLED
// Percorso RF precedente, escluso interamente quando RF_ENABLED vale 0.
RFWirelessTransmitter rFWirelessTransmitter(12, 50, 500);
#endif

void getBatteryLevels();
float readAnalogVoltage(uint8_t analogicPort);
float getBatteryVoltage(float partitionedVoltageBattery, double partitionResistor, double resistor1);
void checkBatteryVoltageLevels(unsigned long now);
bool hasPendingAlarm();
void printBatteryMeasurements();
void printAlarmReason(uint8_t alarmFlags);
void printConfirmedAlarm();
#if RF_ENABLED
void sendMessage(char deviceId[2], char sensorId[2], float voltageValue);
void sendAlarmMessage(char deviceId[2], char sensorId[2], float voltageValue);
#endif

void setup() {
    DEBUG_SERIAL_BEGIN(115200);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH);
    analogReference(DEFAULT);
#if RF_ENABLED
    rFWirelessTransmitter.begin();
#endif
    DEBUG_SERIAL_PRINTLN(F("AVVIO monitor batterie; comando rele'=ON"));
    DEBUG_SERIAL_PRINT(F("Conferma sgancio: "));
    DEBUG_SERIAL_PRINT(REQUIRED_ALARM_READINGS);
    DEBUG_SERIAL_PRINT(F(" scansioni anomale consecutive; almeno "));
    DEBUG_SERIAL_PRINT(ALARM_CONFIRM_TIME_MS);
    DEBUG_SERIAL_PRINTLN(F(" ms dalla prima. Riarmo solo al riavvio."));
    DEBUG_SERIAL_PRINT(F("Vref impostata="));
    DEBUG_SERIAL_PRINT(VOLTAGE_REFERENCE, 3);
    DEBUG_SERIAL_PRINT(F(" V; RF="));
    DEBUG_SERIAL_PRINTLN(RF_ENABLED);
}

void loop() {
    const unsigned long now = millis();
    if (hasMeasurements && now - lastReadingStartMs < READING_INTERVAL_MS) {
        return;
    }
    lastReadingStartMs = now;

#if DEBUG_SERIAL
    const bool hadPendingAlarm = hasPendingAlarm();
    const bool wasTripped = relayTripped;
#endif

    // Prima acquisisce tutti i canali, poi calcola, controlla e stampa.
    getBatteryLevels();
    measurementTimeMs = millis();
    hasMeasurements = true;
    checkBatteryVoltageLevels(measurementTimeMs);

#if DEBUG_SERIAL
    if (!wasTripped && relayTripped) {
        printConfirmedAlarm();
    }
    if (hadPendingAlarm && !hasPendingAlarm() && !relayTripped) {
        DEBUG_SERIAL_PRINTLN(F("ANOMALIA RIENTRATA: conferme azzerate, rele' ancora ON"));
    }
    if (!hasPrintedMeasurements || hadPendingAlarm || hasPendingAlarm() ||
        (!wasTripped && relayTripped) || measurementTimeMs - lastDebugMs >= DEBUG_INTERVAL_MS) {
        lastDebugMs = measurementTimeMs;
        hasPrintedMeasurements = true;
        printBatteryMeasurements();
    }
#endif

#if RF_ENABLED
    // Pubblica un totale gia' acquisito; nessuna trasmissione fra i canali.
    // Durante una conferma non aggiunge le attese della radio.
    if (!hasPendingAlarm()) {
        sendMessage("BI", "A5", batteryVoltageValues[BATTERY_COUNT - 1]);
    }
#endif
}

float readAnalogVoltage(uint8_t analogicPort) {
    // La prima conversione dopo il cambio di ingresso non entra nella media.
    analogRead(analogicPort);
    delayMicroseconds(200);
    uint16_t sum = 0;
    for (uint8_t sample = 0; sample < SAMPLES_PER_CHANNEL; ++sample) {
        sum += analogRead(analogicPort);
    }
    return (static_cast<float>(sum) / SAMPLES_PER_CHANNEL) * VOLTAGE_REFERENCE / 1023.0f;
}

void getBatteryLevels() {
    // Nessuna stampa o comunicazione durante l'acquisizione delle sei prese.
    for (uint8_t channel = 0; channel < BATTERY_COUNT; ++channel) {
        adcVoltageValues[channel] = readAnalogVoltage(ANALOG_PORTS[channel]);
    }

    batteryVoltageValues[0] = getBatteryVoltage(adcVoltageValues[0], R1, 22000.0);
    batteryVoltageValues[1] = getBatteryVoltage(adcVoltageValues[1], R2, COMMONRESISTOR);
    batteryVoltageValues[2] = getBatteryVoltage(adcVoltageValues[2], R3, COMMONRESISTOR);
    batteryVoltageValues[3] = getBatteryVoltage(adcVoltageValues[3], R4, 150000.0);
    batteryVoltageValues[4] = getBatteryVoltage(adcVoltageValues[4], R5, 150000.0);
    batteryVoltageValues[5] = getBatteryVoltage(adcVoltageValues[5], R6, 300000.0);

    // Si usano indici dei canali, senza confrontare gli indirizzi delle UID.
    singleBatteryVoltageValue[0] = batteryVoltageValues[0];
    for (uint8_t channel = 1; channel < BATTERY_COUNT; ++channel) {
        singleBatteryVoltageValue[channel] = batteryVoltageValues[channel] - batteryVoltageValues[channel - 1];
    }
}

void checkBatteryVoltageLevels(unsigned long now) {
    for (uint8_t channel = 0; channel < BATTERY_COUNT; ++channel) {
        const float adcVoltage = adcVoltageValues[channel];
        const float batteryVoltage = singleBatteryVoltageValue[channel];
        uint8_t flags = 0;
        if (isnan(adcVoltage) || isinf(adcVoltage) || isnan(batteryVoltage) || isinf(batteryVoltage)) {
            flags = ALARM_INVALID_READING;
        } else {
            if (adcVoltage < ADC_MIN_VOLTAGE) flags |= ALARM_ADC_LOW;
            if (adcVoltage >= ADC_MAX_VOLTAGE) flags |= ALARM_ADC_HIGH;
            if (batteryVoltage < BATTERY_MIN_VOLTAGE) flags |= ALARM_BATTERY_LOW;
            if (batteryVoltage >= BATTERY_MAX_VOLTAGE) flags |= ALARM_BATTERY_HIGH;
        }
        batteryAlarmFlags[channel] = flags;

        // Le misure proseguono dopo lo sgancio; il rele' non viene riarmato.
        if (relayTripped) {
            continue;
        }
        if (flags == 0) {
            alarmReadingCount[channel] = 0;
            alarmStartMs[channel] = 0;
            continue;
        }
        if (alarmReadingCount[channel] == 0) {
            alarmStartMs[channel] = now;
        }
        if (alarmReadingCount[channel] < REQUIRED_ALARM_READINGS) {
            ++alarmReadingCount[channel];
        }

        // Conta anomalie consecutive dello stesso canale anche se alternano
        // alto/basso. Una lettura completamente regolare azzera la conferma.
        // Il conteggio da solo non basta: deve essere trascorso anche il tempo.
        // La sottrazione unsigned gestisce il rollover di millis().
        if (alarmReadingCount[channel] >= REQUIRED_ALARM_READINGS &&
            now - alarmStartMs[channel] >= ALARM_CONFIRM_TIME_MS) {
            digitalWrite(RELAY_PIN, LOW); // Prima di qualsiasi stampa o invio.
            relayTripped = true;
            tripChannel = channel;
            tripAlarmFlags = flags;
            tripAdcVoltage = adcVoltage;
            tripBatteryVoltage = batteryVoltage;
            tripTimeMs = now;
        }
    }
}

bool hasPendingAlarm() {
    if (relayTripped) {
        return false;
    }
    for (uint8_t channel = 0; channel < BATTERY_COUNT; ++channel) {
        if (alarmReadingCount[channel] != 0) {
            return true;
        }
    }
    return false;
}

void printBatteryMeasurements() {
#if DEBUG_SERIAL
    DEBUG_SERIAL_PRINT(F("t=")); DEBUG_SERIAL_PRINT(measurementTimeMs);
    DEBUG_SERIAL_PRINT(F(" ms; acquisizione="));
    DEBUG_SERIAL_PRINT(measurementTimeMs - lastReadingStartMs);
    DEBUG_SERIAL_PRINT(F(" ms; comando rele'="));
    DEBUG_SERIAL_PRINTLN(relayTripped ? F("OFF BLOCCATO") : F("ON"));
    for (uint8_t channel = 0; channel < BATTERY_COUNT; ++channel) {
        DEBUG_SERIAL_PRINT(F("A")); DEBUG_SERIAL_PRINT(channel);
        DEBUG_SERIAL_PRINT(F(" ADC=")); DEBUG_SERIAL_PRINT(adcVoltageValues[channel], 3);
        DEBUG_SERIAL_PRINT(F(" V; nodo=")); DEBUG_SERIAL_PRINT(batteryVoltageValues[channel], 3);
        DEBUG_SERIAL_PRINT(F(" V; batteria=")); DEBUG_SERIAL_PRINT(singleBatteryVoltageValue[channel], 3);
        DEBUG_SERIAL_PRINT(F(" V; "));
        printAlarmReason(batteryAlarmFlags[channel]);
        if (!relayTripped) {
            DEBUG_SERIAL_PRINT(F("; conferme=")); DEBUG_SERIAL_PRINT(alarmReadingCount[channel]);
            DEBUG_SERIAL_PRINT(F("/")); DEBUG_SERIAL_PRINT(REQUIRED_ALARM_READINGS);
            if (alarmReadingCount[channel] != 0) {
                DEBUG_SERIAL_PRINT(F("; durata="));
                DEBUG_SERIAL_PRINT(measurementTimeMs - alarmStartMs[channel]);
                DEBUG_SERIAL_PRINT(F(" ms"));
            }
        }
        DEBUG_SERIAL_PRINTLN();
    }
#endif
}

void printAlarmReason(uint8_t alarmFlags) {
#if DEBUG_SERIAL
    if (alarmFlags == 0) DEBUG_SERIAL_PRINT(F("regolare"));
    if (alarmFlags & ALARM_ADC_LOW) {
        DEBUG_SERIAL_PRINT(F("ADC sotto ")); DEBUG_SERIAL_PRINT(ADC_MIN_VOLTAGE, 2); DEBUG_SERIAL_PRINT(F(" V "));
    }
    if (alarmFlags & ALARM_ADC_HIGH) {
        DEBUG_SERIAL_PRINT(F("ADC >= ")); DEBUG_SERIAL_PRINT(ADC_MAX_VOLTAGE, 2); DEBUG_SERIAL_PRINT(F(" V "));
    }
    if (alarmFlags & ALARM_BATTERY_LOW) {
        DEBUG_SERIAL_PRINT(F("batteria sotto ")); DEBUG_SERIAL_PRINT(BATTERY_MIN_VOLTAGE, 2); DEBUG_SERIAL_PRINT(F(" V "));
    }
    if (alarmFlags & ALARM_BATTERY_HIGH) {
        DEBUG_SERIAL_PRINT(F("batteria >= ")); DEBUG_SERIAL_PRINT(BATTERY_MAX_VOLTAGE, 2); DEBUG_SERIAL_PRINT(F(" V "));
    }
    if (alarmFlags & ALARM_INVALID_READING) DEBUG_SERIAL_PRINT(F("lettura non valida"));
#else
    (void)alarmFlags;
#endif
}

void printConfirmedAlarm() {
#if DEBUG_SERIAL
    DEBUG_SERIAL_PRINT(F("SGANCIO CONFERMATO t=")); DEBUG_SERIAL_PRINT(tripTimeMs);
    DEBUG_SERIAL_PRINT(F(" ms; batteria=")); DEBUG_SERIAL_PRINT(tripChannel + 1);
    DEBUG_SERIAL_PRINT(F(" (A")); DEBUG_SERIAL_PRINT(tripChannel);
    DEBUG_SERIAL_PRINT(F("); ADC=")); DEBUG_SERIAL_PRINT(tripAdcVoltage, 3);
    DEBUG_SERIAL_PRINT(F(" V; batteria=")); DEBUG_SERIAL_PRINT(tripBatteryVoltage, 3);
    DEBUG_SERIAL_PRINT(F(" V; causa=")); printAlarmReason(tripAlarmFlags);
    DEBUG_SERIAL_PRINTLN(F("; rele' bloccato OFF fino al riavvio"));
#endif
}

#if RF_ENABLED
void sendMessage(char deviceId[2], char sensorId[2], float voltageValue) {
    rFWirelessTransmitter.startTrasmission(deviceId, sensorId, 1);
    rFWirelessTransmitter.sendBufferData(deviceId, sensorId, "V", voltageValue, "0", "0");
    rFWirelessTransmitter.endTrasmission(deviceId, sensorId);
}
void sendAlarmMessage(char deviceId[2], char sensorId[2], float voltageValue) {
    return;
    do {
        rFWirelessTransmitter.startTrasmission(deviceId, sensorId, 1);
        rFWirelessTransmitter.sendBufferData(deviceId, sensorId, "V", voltageValue, "1", "0");
        rFWirelessTransmitter.endTrasmission(deviceId, sensorId);
    } while (true);
}
#endif

float getBatteryVoltage(float partitionedVoltageBattery, double partitionResistor, double resistor1) {
    return ((partitionedVoltageBattery * static_cast<float>(resistor1)) +
        (partitionedVoltageBattery * static_cast<float>(partitionResistor))) / static_cast<float>(partitionResistor);
}



