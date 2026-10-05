// Debug del monitor seriale: 0 = produzione, 1 = debug a 115200 baud.
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
const float COMMON_RESISTOR_R1_R2_R3 = 22000.0f;
const float COMMON_RESISTOR_R4_R5 = 150000.0f;
const float COMMON_RESISTOR_R6 = 300000.0f;

const float r1 = 10100.0f;
const float r2 = 3988.0f;
const float r3 = 2458.0f;
const float r4 = 9928.0f;
const float r5 = 6763.0f;
const float r6 = 12100.0f;

const uint8_t BATTERY_COUNT = 6;
const uint8_t RELAY_PIN = 5;
const uint8_t ANALOG_PORTS[BATTERY_COUNT] = { A0, A1, A2, A3, A4, A5 };
const uint8_t SAMPLES_PER_CHANNEL = 16;

const float VOLTAGE_REFERENCE = 5.00f;

// Soglie originali.
const float ADC_MIN_VOLTAGE = 2.00f;
const float ADC_MAX_VOLTAGE = 4.80f;
const float BATTERY_MIN_VOLTAGE = 11.00f;
const float BATTERY_MAX_VOLTAGE = 14.50f;

const unsigned long READING_INTERVAL_MS = 250UL;
const uint8_t REQUIRED_ALARM_READINGS = 10;
const unsigned long DEBUG_INTERVAL_MS = 1000UL;

const uint8_t ALARM_ADC_LOW = 1;
const uint8_t ALARM_ADC_HIGH = 2;
const uint8_t ALARM_BATTERY_LOW = 4;
const uint8_t ALARM_BATTERY_HIGH = 8;
const uint8_t ALARM_INVALID_READING = 16;

float adc_voltage_values[BATTERY_COUNT] = {};
float battery_voltage_values[BATTERY_COUNT] = {};
float single_battery_voltage_values[BATTERY_COUNT] = {};

uint8_t battery_alarm_flags[BATTERY_COUNT] = {};
uint8_t alarm_reading_count[BATTERY_COUNT] = {};

bool has_measurements = false;
bool relay_cutoff = false;

unsigned long last_reading_start_ms = 0;
unsigned long measurement_time_ms = 0;

// Conserva il primo sgancio fino al riavvio.
uint8_t cutoff_channel = 0;
uint8_t cutoff_alarm_flags = 0;
float cutoff_adc_voltage = 0.0f;
float cutoff_battery_voltage = 0.0f;
unsigned long cutoff_time_ms = 0;

#if DEBUG_SERIAL
unsigned long last_debug_ms = 0;
bool has_printed_measurements = false;
bool debug_stopped = false;
#endif

#if RF_ENABLED
RFWirelessTransmitter rFWirelessTransmitter(12, 50, 500);
#endif

void get_battery_levels();
float read_analog_voltage(uint8_t analogic_port);
float get_battery_voltage(float partitioned_voltage_battery, float partition_resistor, float resistor_1);
void check_battery_voltage_levels(unsigned long now);
bool has_pending_alarm();
void print_battery_measurements();
void print_alarm_reason(uint8_t alarm_flags);
void print_confirmed_alarm();

#if RF_ENABLED
void send_message(char device_id[2], char sensor_id[2], float voltage_value);
void send_alarm_message(char device_id[2], char sensor_id[2], float voltage_value);
#endif

void setup() {
    DEBUG_SERIAL_BEGIN(115200);

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH);

    analogReference(DEFAULT);

#if RF_ENABLED
    rFWirelessTransmitter.begin();
#endif

    DEBUG_SERIAL_PRINTLN(F("START"));
}

void loop() {
    const unsigned long now = millis();

    if (has_measurements && now - last_reading_start_ms < READING_INTERVAL_MS) {
        return;
    }

    last_reading_start_ms = now;

#if DEBUG_SERIAL
    const bool had_pending_alarm = has_pending_alarm();
    const bool was_cutoff = relay_cutoff;
#endif

    // Prima acquisisce tutti i canali, poi calcola e controlla.
    get_battery_levels();

    measurement_time_ms = millis();
    has_measurements = true;

    check_battery_voltage_levels(measurement_time_ms);

#if DEBUG_SERIAL
    if (!debug_stopped) {
        if (!was_cutoff && relay_cutoff) {
            // Ultima fotografia completa delle sei batterie.
            print_battery_measurements();

            // Questa rimane l'ultima riga del log.
            print_confirmed_alarm();

            // Da questo momento non viene piu' scritto nulla sulla seriale.
            debug_stopped = true;
        }
        else {
            if (had_pending_alarm && !has_pending_alarm()) {
                DEBUG_SERIAL_PRINTLN(F("ALARM_CLEAR"));
            }

            if (!has_printed_measurements ||
                had_pending_alarm ||
                has_pending_alarm() ||
                measurement_time_ms - last_debug_ms >= DEBUG_INTERVAL_MS) {

                last_debug_ms = measurement_time_ms;
                has_printed_measurements = true;

                print_battery_measurements();
            }
        }
    }
#endif

#if RF_ENABLED
    if (!has_pending_alarm()) {
        send_message("BI", "A5", battery_voltage_values[BATTERY_COUNT - 1]);
    }
#endif
}

float read_analog_voltage(uint8_t analogic_port) {
    // La prima conversione dopo il cambio di ingresso non entra nella media.
    analogRead(analogic_port);

    delayMicroseconds(200);

    uint16_t sum = 0;

    for (uint8_t sample = 0; sample < SAMPLES_PER_CHANNEL; ++sample) {
        sum += analogRead(analogic_port);
    }

    return (static_cast<float>(sum) / SAMPLES_PER_CHANNEL) * VOLTAGE_REFERENCE / 1023.0f;
}

void get_battery_levels() {
    // Nessuna stampa o comunicazione durante l'acquisizione delle sei prese.
    for (uint8_t channel = 0; channel < BATTERY_COUNT; ++channel) {
        adc_voltage_values[channel] = read_analog_voltage(ANALOG_PORTS[channel]);
    }

    battery_voltage_values[0] = get_battery_voltage(adc_voltage_values[0], r1, COMMON_RESISTOR_R1_R2_R3);
    battery_voltage_values[1] = get_battery_voltage(adc_voltage_values[1], r2, COMMON_RESISTOR_R1_R2_R3);
    battery_voltage_values[2] = get_battery_voltage(adc_voltage_values[2], r3, COMMON_RESISTOR_R1_R2_R3);
    battery_voltage_values[3] = get_battery_voltage(adc_voltage_values[3], r4, COMMON_RESISTOR_R4_R5);
    battery_voltage_values[4] = get_battery_voltage(adc_voltage_values[4], r5, COMMON_RESISTOR_R4_R5);
    battery_voltage_values[5] = get_battery_voltage(adc_voltage_values[5], r6, COMMON_RESISTOR_R6);

    single_battery_voltage_values[0] = battery_voltage_values[0];

    for (uint8_t channel = 1; channel < BATTERY_COUNT; ++channel) {
        single_battery_voltage_values[channel] = battery_voltage_values[channel] - battery_voltage_values[channel - 1];
    }
}

void check_battery_voltage_levels(unsigned long now) {
    for (uint8_t channel = 0; channel < BATTERY_COUNT; ++channel) {
        const float adc_voltage = adc_voltage_values[channel];
        const float battery_voltage = single_battery_voltage_values[channel];

        uint8_t alarm_flags = 0;

        if (isnan(adc_voltage) ||
            isinf(adc_voltage) ||
            isnan(battery_voltage) ||
            isinf(battery_voltage)) {

            alarm_flags = ALARM_INVALID_READING;
        }
        else {
            if (adc_voltage < ADC_MIN_VOLTAGE) alarm_flags |= ALARM_ADC_LOW;
            if (adc_voltage >= ADC_MAX_VOLTAGE) alarm_flags |= ALARM_ADC_HIGH;
            if (battery_voltage < BATTERY_MIN_VOLTAGE) alarm_flags |= ALARM_BATTERY_LOW;
            if (battery_voltage >= BATTERY_MAX_VOLTAGE) alarm_flags |= ALARM_BATTERY_HIGH;
        }

        battery_alarm_flags[channel] = alarm_flags;

        // Le misure proseguono dopo lo sgancio; il rele' non viene riarmato.
        if (relay_cutoff) {
            continue;
        }

        // Per lo sgancio contano esclusivamente le anomalie HIGH.
        // Ogni batteria mantiene il proprio contatore indipendente.
        const bool high_alarm =
            (alarm_flags & ALARM_ADC_HIGH) ||
            (alarm_flags & ALARM_BATTERY_HIGH);

        // Una singola lettura non-HIGH interrompe la sequenza
        // e azzera il contatore della batteria.
        if (!high_alarm) {
            alarm_reading_count[channel] = 0;
            continue;
        }

        if (alarm_reading_count[channel] < REQUIRED_ALARM_READINGS) {
            ++alarm_reading_count[channel];
        }

        // Lo sgancio avviene esclusivamente dopo 10 letture HIGH
        // consecutive sulla stessa batteria.
        if (alarm_reading_count[channel] >= REQUIRED_ALARM_READINGS) {
            // Il rele' viene sganciato prima di qualsiasi stampa.
            digitalWrite(RELAY_PIN, LOW);

            relay_cutoff = true;

            cutoff_channel = channel;
            cutoff_alarm_flags = alarm_flags;
            cutoff_adc_voltage = adc_voltage;
            cutoff_battery_voltage = battery_voltage;
            cutoff_time_ms = now;
        }
    }
}

bool has_pending_alarm() {
    if (relay_cutoff) {
        return false;
    }

    for (uint8_t channel = 0; channel < BATTERY_COUNT; ++channel) {
        if (alarm_reading_count[channel] != 0) {
            return true;
        }
    }

    return false;
}

void print_battery_measurements() {
#if DEBUG_SERIAL
    DEBUG_SERIAL_PRINT(F("t="));
    DEBUG_SERIAL_PRINT(measurement_time_ms);

    DEBUG_SERIAL_PRINT(F("ms relay="));
    DEBUG_SERIAL_PRINTLN(relay_cutoff ? F("OFF") : F("ON"));

    for (uint8_t channel = 0; channel < BATTERY_COUNT; ++channel) {
        DEBUG_SERIAL_PRINT(F("B"));
        DEBUG_SERIAL_PRINT(channel + 1);

        DEBUG_SERIAL_PRINT(F(" ADC="));
        DEBUG_SERIAL_PRINT(adc_voltage_values[channel], 3);

        DEBUG_SERIAL_PRINT(F(" NODE="));
        DEBUG_SERIAL_PRINT(battery_voltage_values[channel], 3);

        DEBUG_SERIAL_PRINT(F(" BAT="));
        DEBUG_SERIAL_PRINT(single_battery_voltage_values[channel], 3);

        DEBUG_SERIAL_PRINT(F(" "));
        print_alarm_reason(battery_alarm_flags[channel]);

        if (!relay_cutoff) {
            DEBUG_SERIAL_PRINT(F(" C="));
            DEBUG_SERIAL_PRINT(alarm_reading_count[channel]);

            DEBUG_SERIAL_PRINT(F("/"));
            DEBUG_SERIAL_PRINT(REQUIRED_ALARM_READINGS);
        }

        DEBUG_SERIAL_PRINTLN();
    }
#endif
}

void print_alarm_reason(uint8_t alarm_flags) {
#if DEBUG_SERIAL
    if (alarm_flags == 0) {
        DEBUG_SERIAL_PRINT(F("OK"));
        return;
    }

    bool separator_required = false;

    if (alarm_flags & ALARM_ADC_LOW) {
        DEBUG_SERIAL_PRINT(F("ADC_LOW<"));
        DEBUG_SERIAL_PRINT(ADC_MIN_VOLTAGE, 2);
        separator_required = true;
    }

    if (alarm_flags & ALARM_ADC_HIGH) {
        if (separator_required) DEBUG_SERIAL_PRINT(F("|"));

        DEBUG_SERIAL_PRINT(F("ADC_HIGH>="));
        DEBUG_SERIAL_PRINT(ADC_MAX_VOLTAGE, 2);

        separator_required = true;
    }

    if (alarm_flags & ALARM_BATTERY_LOW) {
        if (separator_required) DEBUG_SERIAL_PRINT(F("|"));

        DEBUG_SERIAL_PRINT(F("BAT_LOW<"));
        DEBUG_SERIAL_PRINT(BATTERY_MIN_VOLTAGE, 2);

        separator_required = true;
    }

    if (alarm_flags & ALARM_BATTERY_HIGH) {
        if (separator_required) DEBUG_SERIAL_PRINT(F("|"));

        DEBUG_SERIAL_PRINT(F("BAT_HIGH>="));
        DEBUG_SERIAL_PRINT(BATTERY_MAX_VOLTAGE, 2);

        separator_required = true;
    }

    if (alarm_flags & ALARM_INVALID_READING) {
        if (separator_required) DEBUG_SERIAL_PRINT(F("|"));

        DEBUG_SERIAL_PRINT(F("INVALID"));
    }
#else
    (void)alarm_flags;
#endif
}

void print_confirmed_alarm() {
#if DEBUG_SERIAL
    DEBUG_SERIAL_PRINT(F("CUTOFF t="));
    DEBUG_SERIAL_PRINT(cutoff_time_ms);

    DEBUG_SERIAL_PRINT(F("ms B="));
    DEBUG_SERIAL_PRINT(cutoff_channel + 1);

    DEBUG_SERIAL_PRINT(F(" A="));
    DEBUG_SERIAL_PRINT(cutoff_channel);

    DEBUG_SERIAL_PRINT(F(" ADC="));
    DEBUG_SERIAL_PRINT(cutoff_adc_voltage, 3);

    DEBUG_SERIAL_PRINT(F(" BAT="));
    DEBUG_SERIAL_PRINT(cutoff_battery_voltage, 3);

    DEBUG_SERIAL_PRINT(F(" CAUSE="));
    print_alarm_reason(cutoff_alarm_flags);

    DEBUG_SERIAL_PRINTLN(F(" RELAY=OFF"));
#endif
}

#if RF_ENABLED
void send_message(char device_id[2], char sensor_id[2], float voltage_value) {
    rFWirelessTransmitter.startTrasmission(device_id, sensor_id, 1);
    rFWirelessTransmitter.sendBufferData(device_id, sensor_id, "V", voltage_value, "0", "0");
    rFWirelessTransmitter.endTrasmission(device_id, sensor_id);
}

void send_alarm_message(char device_id[2], char sensor_id[2], float voltage_value) {
    return;

    do {
        rFWirelessTransmitter.startTrasmission(device_id, sensor_id, 1);
        rFWirelessTransmitter.sendBufferData(device_id, sensor_id, "V", voltage_value, "1", "0");
        rFWirelessTransmitter.endTrasmission(device_id, sensor_id);
    } while (true);
}
#endif

float get_battery_voltage(float partitioned_voltage_battery, float partition_resistor, float resistor_1) {
    return ((partitioned_voltage_battery * resistor_1) +
        (partitioned_voltage_battery * partition_resistor)) /
        partition_resistor;
}