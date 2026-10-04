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
const double COMMON_RESISTOR_R1_R2_R3 = 22000.0;
const double COMMON_RESISTOR_R4_R5 = 150000.0;
const double COMMON_RESISTOR_R6 = 300000.0;

int r1 = 10100;
int r2 = 3988;
int r3 = 2458;
double r4 = 9928;
int r5 = 6763;
int r6 = 12100;

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

float adc_voltage_values[BATTERY_COUNT] = {};
float battery_voltage_values[BATTERY_COUNT] = {};
float single_battery_voltage_values[BATTERY_COUNT] = {};

uint8_t battery_alarm_flags[BATTERY_COUNT] = {};
uint8_t alarm_reading_count[BATTERY_COUNT] = {};
unsigned long alarm_start_ms[BATTERY_COUNT] = {};

bool has_measurements = false;
bool relay_cutoff = false;

unsigned long last_reading_start_ms = 0;
unsigned long measurement_time_ms = 0;

// Conserva il primo sgancio fino al riavvio, anche se le tensioni rientrano.
uint8_t cutoff_channel = 0;
uint8_t cutoff_alarm_flags = 0;
float cutoff_adc_voltage = 0.0f;
float cutoff_battery_voltage = 0.0f;
unsigned long cutoff_time_ms = 0;

#if DEBUG_SERIAL
unsigned long last_debug_ms = 0;
bool has_printed_measurements = false;
#endif

#if RF_ENABLED
// Oggetto: manteniamo il nome originale.
RFWirelessTransmitter rFWirelessTransmitter(12, 50, 500);
#endif

void get_battery_levels();
float read_analog_voltage(uint8_t analogic_port);
float get_battery_voltage(float partitioned_voltage_battery, double partition_resistor, double resistor_1);
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

    if (has_measurements && now - last_reading_start_ms < READING_INTERVAL_MS) {
        return;
    }

    last_reading_start_ms = now;

#if DEBUG_SERIAL
    const bool had_pending_alarm = has_pending_alarm();
    const bool was_cutoff = relay_cutoff;
#endif

    // Prima acquisisce tutti i canali, poi calcola, controlla e stampa.
    get_battery_levels();

    measurement_time_ms = millis();
    has_measurements = true;

    check_battery_voltage_levels(measurement_time_ms);

#if DEBUG_SERIAL
    if (!was_cutoff && relay_cutoff) {
        print_confirmed_alarm();
    }

    if (had_pending_alarm && !has_pending_alarm() && !relay_cutoff) {
        DEBUG_SERIAL_PRINTLN(F("ANOMALIA RIENTRATA: conferme azzerate, rele' ancora ON"));
    }

    if (!has_printed_measurements ||
        had_pending_alarm ||
        has_pending_alarm() ||
        (!was_cutoff && relay_cutoff) ||
        measurement_time_ms - last_debug_ms >= DEBUG_INTERVAL_MS) {

        last_debug_ms = measurement_time_ms;
        has_printed_measurements = true;
        print_battery_measurements();
    }
#endif

#if RF_ENABLED
    // Pubblica un totale gia' acquisito; nessuna trasmissione fra i canali.
    // Durante una conferma non aggiunge le attese della radio.
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

    // Si usano indici dei canali, senza confrontare gli indirizzi delle UID.
    single_battery_voltage_values[0] = battery_voltage_values[0];

    for (uint8_t channel = 1; channel < BATTERY_COUNT; ++channel) {
        single_battery_voltage_values[channel] = battery_voltage_values[channel] - battery_voltage_values[channel - 1];
    }
}

void check_battery_voltage_levels(unsigned long now) {
    for (uint8_t channel = 0; channel < BATTERY_COUNT; ++channel) {
        const float adc_voltage = adc_voltage_values[channel];
        const float battery_voltage = single_battery_voltage_values[channel];

        uint8_t flags = 0;

        if (isnan(adc_voltage) ||
            isinf(adc_voltage) ||
            isnan(battery_voltage) ||
            isinf(battery_voltage)) {

            flags = ALARM_INVALID_READING;
        }
        else {
            if (adc_voltage < ADC_MIN_VOLTAGE) flags |= ALARM_ADC_LOW;
            if (adc_voltage >= ADC_MAX_VOLTAGE) flags |= ALARM_ADC_HIGH;
            if (battery_voltage < BATTERY_MIN_VOLTAGE) flags |= ALARM_BATTERY_LOW;
            if (battery_voltage >= BATTERY_MAX_VOLTAGE) flags |= ALARM_BATTERY_HIGH;
        }

        battery_alarm_flags[channel] = flags;

        // Le misure proseguono dopo lo sgancio; il rele' non viene riarmato.
        if (relay_cutoff) {
            continue;
        }

        if (flags == 0) {
            alarm_reading_count[channel] = 0;
            alarm_start_ms[channel] = 0;
            continue;
        }

        if (alarm_reading_count[channel] == 0) {
            alarm_start_ms[channel] = now;
        }

        if (alarm_reading_count[channel] < REQUIRED_ALARM_READINGS) {
            ++alarm_reading_count[channel];
        }

        // Conta anomalie consecutive dello stesso canale anche se alternano
        // alto/basso. Una lettura completamente regolare azzera la conferma.
        // Il conteggio da solo non basta: deve essere trascorso anche il tempo.
        // La sottrazione unsigned gestisce il rollover di millis().
        if (alarm_reading_count[channel] >= REQUIRED_ALARM_READINGS &&
            now - alarm_start_ms[channel] >= ALARM_CONFIRM_TIME_MS) {

            digitalWrite(RELAY_PIN, LOW); // Prima di qualsiasi stampa o invio.

            relay_cutoff = true;

            cutoff_channel = channel;
            cutoff_alarm_flags = flags;
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

    DEBUG_SERIAL_PRINT(F(" ms; acquisizione="));
    DEBUG_SERIAL_PRINT(measurement_time_ms - last_reading_start_ms);

    DEBUG_SERIAL_PRINT(F(" ms; comando rele'="));
    DEBUG_SERIAL_PRINTLN(relay_cutoff ? F("OFF BLOCCATO") : F("ON"));

    for (uint8_t channel = 0; channel < BATTERY_COUNT; ++channel) {
        DEBUG_SERIAL_PRINT(F("A"));
        DEBUG_SERIAL_PRINT(channel);

        DEBUG_SERIAL_PRINT(F(" ADC="));
        DEBUG_SERIAL_PRINT(adc_voltage_values[channel], 3);

        DEBUG_SERIAL_PRINT(F(" V; nodo="));
        DEBUG_SERIAL_PRINT(battery_voltage_values[channel], 3);

        DEBUG_SERIAL_PRINT(F(" V; batteria="));
        DEBUG_SERIAL_PRINT(single_battery_voltage_values[channel], 3);

        DEBUG_SERIAL_PRINT(F(" V; "));

        print_alarm_reason(battery_alarm_flags[channel]);

        if (!relay_cutoff) {
            DEBUG_SERIAL_PRINT(F("; conferme="));
            DEBUG_SERIAL_PRINT(alarm_reading_count[channel]);

            DEBUG_SERIAL_PRINT(F("/"));
            DEBUG_SERIAL_PRINT(REQUIRED_ALARM_READINGS);

            if (alarm_reading_count[channel] != 0) {
                DEBUG_SERIAL_PRINT(F("; durata="));
                DEBUG_SERIAL_PRINT(measurement_time_ms - alarm_start_ms[channel]);
                DEBUG_SERIAL_PRINT(F(" ms"));
            }
        }

        DEBUG_SERIAL_PRINTLN();
    }
#endif
}

void print_alarm_reason(uint8_t alarm_flags) {
#if DEBUG_SERIAL
    if (alarm_flags == 0) {
        DEBUG_SERIAL_PRINT(F("regolare"));
    }

    if (alarm_flags & ALARM_ADC_LOW) {
        DEBUG_SERIAL_PRINT(F("ADC sotto "));
        DEBUG_SERIAL_PRINT(ADC_MIN_VOLTAGE, 2);
        DEBUG_SERIAL_PRINT(F(" V "));
    }

    if (alarm_flags & ALARM_ADC_HIGH) {
        DEBUG_SERIAL_PRINT(F("ADC >= "));
        DEBUG_SERIAL_PRINT(ADC_MAX_VOLTAGE, 2);
        DEBUG_SERIAL_PRINT(F(" V "));
    }

    if (alarm_flags & ALARM_BATTERY_LOW) {
        DEBUG_SERIAL_PRINT(F("batteria sotto "));
        DEBUG_SERIAL_PRINT(BATTERY_MIN_VOLTAGE, 2);
        DEBUG_SERIAL_PRINT(F(" V "));
    }

    if (alarm_flags & ALARM_BATTERY_HIGH) {
        DEBUG_SERIAL_PRINT(F("batteria >= "));
        DEBUG_SERIAL_PRINT(BATTERY_MAX_VOLTAGE, 2);
        DEBUG_SERIAL_PRINT(F(" V "));
    }

    if (alarm_flags & ALARM_INVALID_READING) {
        DEBUG_SERIAL_PRINT(F("lettura non valida"));
    }
#else
    (void)alarm_flags;
#endif
}

void print_confirmed_alarm() {
#if DEBUG_SERIAL
    DEBUG_SERIAL_PRINT(F("SGANCIO CONFERMATO t="));
    DEBUG_SERIAL_PRINT(cutoff_time_ms);

    DEBUG_SERIAL_PRINT(F(" ms; batteria="));
    DEBUG_SERIAL_PRINT(cutoff_channel + 1);

    DEBUG_SERIAL_PRINT(F(" (A"));
    DEBUG_SERIAL_PRINT(cutoff_channel);

    DEBUG_SERIAL_PRINT(F("); ADC="));
    DEBUG_SERIAL_PRINT(cutoff_adc_voltage, 3);

    DEBUG_SERIAL_PRINT(F(" V; batteria="));
    DEBUG_SERIAL_PRINT(cutoff_battery_voltage, 3);

    DEBUG_SERIAL_PRINT(F(" V; causa="));
    print_alarm_reason(cutoff_alarm_flags);

    DEBUG_SERIAL_PRINTLN(F("; rele' bloccato OFF fino al riavvio"));
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

float get_battery_voltage(float partitioned_voltage_battery, double partition_resistor, double resistor_1) {
    return ((partitioned_voltage_battery * static_cast<float>(resistor_1)) +
        (partitioned_voltage_battery * static_cast<float>(partition_resistor))) /
        static_cast<float>(partition_resistor);
}