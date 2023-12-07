#include <eRCaGuy_analogReadXXbit.h>
#include <VoltageSensor.h>
#include <VirtualWire.h>
#include <RFWirelessTransmitter.h>

int COMMONRESISTOR = 22000;
int R1 = 10100;
int R2 = 3988;
int R3 = 2458;
double R4 = 9928;
int R5 = 6763;
int R6 = 12100;

float batteryVoltageValues[6] = {0.00,0.00,0.00,0.00,0.00,0.00};

float singleBatteryVoltageValue[6] = { 0.00,0.00,0.00,0.00,0.00,0.00 };

VoltageSensor* voltageSensor;

RFWirelessTransmitter rFWirelessTransmitter(12, 50, 500);
//char deviceId[2] = { 'B','I' };";

void setup()
{
	Serial.begin(115200);
	pinMode(5, OUTPUT);
	digitalWrite(5, HIGH);
	rFWirelessTransmitter.begin();
}

void loop()
{
	voltageSensorActivity(A0, "A0");
	voltageSensorActivity(A1, "A1");
	voltageSensorActivity(A2, "A2");
	voltageSensorActivity(A3, "A3");
	voltageSensorActivity(A4, "A4");
	voltageSensorActivity(A5, "A5");
}

void voltageSensorActivity(uint8_t analogicPort, char uid[2])
{
	delay(100);
	VoltageSensor* voltageSensor = new VoltageSensor(analogicPort, 4.80, 2.00, 5, uid);
	voltageSensor->SetDigitalAlarmPinOut(13);
	getBatteryLevels(*voltageSensor);
	delete voltageSensor;
}

bool getBatteryLevels(VoltageSensor voltageSensor)
{
	Serial.print("Sensor UID "); Serial.print(voltageSensor.UID()); Serial.print(" : "); Serial.println(voltageSensor.GetSegnalValue());
	
	if (voltageSensor.UID() == "A0")
	{
		float batteryVoltage = voltageSensor.GetSegnalValue();
		batteryVoltageValues[0] = getBatteryVoltage(batteryVoltage, R1,22000);
		Serial.println(batteryVoltageValues[0]);
		singleBatteryVoltageValue[0] = batteryVoltageValues[0];
		Serial.print("Singola batteria voltaggio :"); Serial.println(singleBatteryVoltageValue[0]);
		Serial.println();
		//Serial.print("Voltaggio batteria in serie :"); Serial.println(batteryVoltageValues[0]);
		checkBatteryVoltageLevel(voltageSensor, batteryVoltage,  singleBatteryVoltageValue[0]);
	}
	if (voltageSensor.UID() == "A1")
	{
		float batteryVoltage = voltageSensor.GetSegnalValue();
		batteryVoltageValues[1] = getBatteryVoltage(batteryVoltage, R2, COMMONRESISTOR);
		Serial.println(batteryVoltageValues[1]);
		singleBatteryVoltageValue[1] = batteryVoltageValues[1] - batteryVoltageValues[0];
		Serial.print("Singola batteria voltaggio :"); Serial.println(singleBatteryVoltageValue[1]);
		Serial.println();
		//Serial.print("Voltaggio batteria in serie :"); Serial.println(batteryVoltageValues[1]);
		checkBatteryVoltageLevel(voltageSensor, batteryVoltage, singleBatteryVoltageValue[1]);
	}

	if (voltageSensor.UID() == "A2")
	{
		float batteryVoltage = voltageSensor.GetSegnalValue();
		batteryVoltageValues[2] = getBatteryVoltage(batteryVoltage, R3, COMMONRESISTOR);
		Serial.println(batteryVoltageValues[2]);
		singleBatteryVoltageValue[2] = batteryVoltageValues[2] - batteryVoltageValues[1];
		Serial.print("Singola batteria voltaggio :"); Serial.println(singleBatteryVoltageValue[2]);
		Serial.println();
		//Serial.print("Voltaggio batteria in serie :"); Serial.println(batteryVoltageValues[2]);
		checkBatteryVoltageLevel(voltageSensor, batteryVoltage, singleBatteryVoltageValue[2]);
	}


	
	if (voltageSensor.UID() == "A3")
	{
		float batteryVoltage = voltageSensor.GetSegnalValue();
		double test = 150000;
		batteryVoltageValues[3] = getBatteryVoltage(batteryVoltage, R4, test);
		Serial.println(batteryVoltageValues[3]);
		singleBatteryVoltageValue[3] = batteryVoltageValues[3] - batteryVoltageValues[2];
		Serial.print("Singola batteria voltaggio :"); Serial.println(singleBatteryVoltageValue[3]);
		Serial.println();
		//Serial.print("Voltaggio batteria in serie :"); Serial.println(batteryVoltageValues[3]);
		checkBatteryVoltageLevel(voltageSensor, batteryVoltage, singleBatteryVoltageValue[3]);
	}

	if (voltageSensor.UID() == "A4")
	{
		float batteryVoltage = voltageSensor.GetSegnalValue();
		batteryVoltageValues[4] = getBatteryVoltage(batteryVoltage, R5, 150000);
		Serial.println(batteryVoltageValues[4]);
		singleBatteryVoltageValue[4] = batteryVoltageValues[4] - batteryVoltageValues[3];
		Serial.print("Singola batteria voltaggio :"); Serial.println(singleBatteryVoltageValue[4]);
		Serial.println();
		//Serial.print("Voltaggio batteria in serie :"); Serial.println(batteryVoltageValues[4]);
		checkBatteryVoltageLevel(voltageSensor, batteryVoltage, singleBatteryVoltageValue[4]);
	}

	if (voltageSensor.UID() == "A5")
	{
		float batteryVoltage = voltageSensor.GetSegnalValue();
		batteryVoltageValues[5] = getBatteryVoltage(batteryVoltage, R6, 300000);
		Serial.println(batteryVoltageValues[5]);
		singleBatteryVoltageValue[5] = batteryVoltageValues[5] - batteryVoltageValues[4];
		Serial.print("Singola batteria voltaggio :"); Serial.println(singleBatteryVoltageValue[5]);
		Serial.println();
		Serial.println("-----------------------------------");
		Serial.print("Voltaggio batteria :"); Serial.println(batteryVoltageValues[5]);
		Serial.println("-----------------------------------");
		Serial.println();
		checkBatteryVoltageLevel(voltageSensor, batteryVoltage, singleBatteryVoltageValue[5]);
	}
	sendMessage("BI", "A5", batteryVoltageValues[5]);
}

void checkBatteryVoltageLevel(VoltageSensor voltageSensor, float batteryVoltage,float voltageSingleBattery)
{
	if (batteryVoltage >= voltageSensor.GetMaxValue() || batteryVoltage < voltageSensor.GetMinValue())
	{
		digitalWrite(5, LOW);
		sendAlarmMessage("BI", voltageSensor.UID(), batteryVoltage);
	}
	if (voltageSingleBattery >= 14.50 || voltageSingleBattery < 11.00)
	{
		digitalWrite(5, LOW);
		sendAlarmMessage("BI", voltageSensor.UID(), voltageSingleBattery);
	}
}

void sendMessage(char deviceId[2], char sensorId[2], float voltageValue)
{
		rFWirelessTransmitter.startTrasmission(deviceId, sensorId, 1);
		rFWirelessTransmitter.sendBufferData(deviceId,sensorId,"V", voltageValue, "0", "0");
		rFWirelessTransmitter.endTrasmission(deviceId, sensorId);
}

void sendAlarmMessage(char deviceId[2], char sensorId[2], float voltageValue)
{
	return;
	do {
		rFWirelessTransmitter.startTrasmission(deviceId, sensorId, 1);
		rFWirelessTransmitter.sendBufferData(deviceId, sensorId, "V", voltageValue, "1", "0");
		rFWirelessTransmitter.endTrasmission(deviceId, sensorId);
	} while (true);
}

float getBatteryVoltage(float partitionedVoltageBattery, double partitionResistor,double restitor1)
{
	Serial.println(restitor1);
	Serial.println(partitionResistor);

	float returnValue = ((partitionedVoltageBattery * (float)restitor1) + (partitionedVoltageBattery * (float)partitionResistor)) / (float)partitionResistor;
	return returnValue;
}



