#include <HardwareSerial.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define NODE_ID       "ND1"
#define LORA_PSK      "WGW2026"

#define DHTPIN        14
#define ONE_WIRE_BUS  9
#define PH_PIN        5
#define TURBIDITY_PIN 6
#define DO_PIN        7
#define EC_PIN        8

#define LORA_AUX      4
#define LORA_RX_PIN   18
#define LORA_TX_PIN   17

#define RELAY_1       40
#define RELAY_2       41
#define RELAY_3       42
#define RELAY_ON      LOW
#define RELAY_OFF     HIGH

DHT dht(DHTPIN, DHT11);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
HardwareSerial LoRaSerial(2);

DeviceAddress waterTempSensor = { 0x28, 0x4F, 0x30, 0xA3, 0x75, 0x25, 0x0B, 0x2E };
unsigned long sampleSeq = 1;
bool relayState[3] = {false, false, false};

float pH_Offset   = 24.442;
float kValue_Low  = 1.964;
float kValue_High = 1.197;
float DO_Volt_0   = 0.180;
float DO_Volt_100 = 1.975;
float turb_clean  = 4.85;
float turb_muddy  = 2.50;

float getSaturationDO(float tempC) {
  return 14.652 - (0.41022 * tempC) + (0.007991 * tempC * tempC) - (0.000077774 * tempC * tempC * tempC);
}

void applyRelays() {
  digitalWrite(RELAY_1, relayState[0] ? RELAY_ON : RELAY_OFF);
  digitalWrite(RELAY_2, relayState[1] ? RELAY_ON : RELAY_OFF);
  digitalWrite(RELAY_3, relayState[2] ? RELAY_ON : RELAY_OFF);
}

void handleGatewayCommand(char* line) {
  char* tok[8];
  int n = 0;
  char* p = strtok(line, ",");
  while (p && n < 8) {
    tok[n++] = p;
    p = strtok(NULL, ",");
  }
  if (n < 6) return;
  if (strcmp(tok[0], LORA_PSK) != 0) return;
  if (strcmp(tok[1], "CMD") != 0) return;
  if (strcasecmp(tok[2], NODE_ID) != 0 && strcasecmp(tok[2], "ALL") != 0) return;

  relayState[0] = (atoi(tok[3]) != 0);
  relayState[1] = (atoi(tok[4]) != 0);
  relayState[2] = (atoi(tok[5]) != 0);
  applyRelays();
}

void pollLoRaCommands() {
  static char rxLine[96];
  static int rxLen = 0;
  while (LoRaSerial.available()) {
    char c = LoRaSerial.read();
    if (c == '\n') {
      if (rxLen > 0) {
        rxLine[rxLen] = 0;
        handleGatewayCommand(rxLine);
      }
      rxLen = 0;
    } else if (c != '\r') {
      if (rxLen < (int)sizeof(rxLine) - 1) rxLine[rxLen++] = c;
      else rxLen = 0;
    }
  }
}

float getAverageADC(int pin) {
  long sum = 0;
  for (int i = 0; i < 50; i++) {
    sum += analogRead(pin);
    pollLoRaCommands();
    delayMicroseconds(500);
  }
  return sum / 50.0;
}

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_1, OUTPUT);
  pinMode(RELAY_2, OUTPUT);
  pinMode(RELAY_3, OUTPUT);
  applyRelays();

  dht.begin();
  ds18b20.begin();
  ds18b20.setResolution(waterTempSensor, 12);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(LORA_AUX, INPUT);
  LoRaSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
}

void loop() {
  pollLoRaCommands();

  static unsigned long lastSend = 0;
  if (millis() - lastSend >= 5000) {
    lastSend = millis();

    float airTemp = dht.readTemperature();
    if (isnan(airTemp)) airTemp = 0.0;

    ds18b20.requestTemperatures();
    pollLoRaCommands();
    float waterTemp = ds18b20.getTempC(waterTempSensor);
    if (waterTemp == DEVICE_DISCONNECTED_C) waterTemp = 25.0;

    float v_pH = getAverageADC(PH_PIN) * (3.3 / 4095.0) * 2.0;
    float pH_Value = (-5.70 * v_pH) + pH_Offset;
    if (pH_Value < 0.0) pH_Value = 0.0;
    if (pH_Value > 14.0) pH_Value = 14.0;

    float v_Turb = getAverageADC(TURBIDITY_PIN) * (3.3 / 4095.0) * 2.0;
    float Turb_Value = 0.0;
    if (v_Turb <= turb_muddy) {
      Turb_Value = 100.0;
    } else if (v_Turb < turb_clean) {
      Turb_Value = 100.0 - ((v_Turb - turb_muddy) / (turb_clean - turb_muddy) * 100.0);
    }

    float v_EC = getAverageADC(EC_PIN) * (3.3 / 4095.0);
    float compCoefficient = 1.0 + 0.0185 * (waterTemp - 25.0);
    float v_EC_Comp_mV = (v_EC / compCoefficient) * 1000.0;
    float EC_Value = (v_EC_Comp_mV < 500.0) ? ((v_EC_Comp_mV * kValue_Low) / 164.0) : ((v_EC_Comp_mV * kValue_High) / 164.0);

    float v_DO = getAverageADC(DO_PIN) * (3.3 / 4095.0);
    float DO_Max = getSaturationDO(waterTemp);
    float DO_Value = 0.0;
    if (v_DO >= DO_Volt_100) {
      DO_Value = DO_Max;
    } else if (v_DO > DO_Volt_0) {
      DO_Value = ((v_DO - DO_Volt_0) / (DO_Volt_100 - DO_Volt_0)) * DO_Max;
    }

    char buffer[150];
    snprintf(buffer, sizeof(buffer), "%s,%s,%lu,%.1f,%.1f,%.2f,%.1f,%.2f,%.2f\n",
             LORA_PSK, NODE_ID, sampleSeq++, waterTemp, airTemp, pH_Value, Turb_Value, DO_Value, EC_Value);

    LoRaSerial.print(buffer);
  }
}
