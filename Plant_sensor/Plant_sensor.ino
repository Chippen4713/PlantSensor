#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <DHT.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_sleep.h"

// =====================
// Sensor pins
// =====================
#define SOIL_PIN   0
#define DHTPIN     1
#define LDR_PIN    2
#define DHTTYPE    DHT11

// =====================
// RGB LED pins
// =====================
#define LED_RED    9
#define LED_GREEN  10

const bool LED_COMMON_ANODE = false;

// =====================
// LoRa pins for ESP32-C3
// =====================
#define LORA_SCK   4
#define LORA_MISO  5
#define LORA_MOSI  6
#define LORA_SS    7
#define LORA_RST   8
#define LORA_DIO0  3

// =====================
// Deep sleep timing
// =====================
#define SLEEP_INTERVAL_US (15ULL * 60ULL * 1000000ULL)   // 15 min

// =====================
// Calibration values
// =====================
const int SOIL_DRY   = 2876;
const int SOIL_WET   = 1941;

const int LIGHT_DARK   = 4095;
const int LIGHT_BRIGHT = 1130;

// =====================
// Thresholds
// =====================
const int MOISTURE_TOO_DRY_PCT = 35;
const int MOISTURE_GOOD_PCT    = 60;
const int LIGHT_TOO_DARK_PCT   = 20;

// keep seq across deep sleep
RTC_DATA_ATTR uint16_t seqCounter = 0;

DHT dht(DHTPIN, DHTTYPE);

// =====================
// Sequence helper
// =====================
uint16_t nextSeq() {
  seqCounter++;
  if (seqCounter == 0) {
    seqCounter = 1;
  }
  return seqCounter;
}

// =====================
// LED helpers
// =====================
void ledRed(bool on) {
  digitalWrite(LED_RED, LED_COMMON_ANODE ? !on : on);
}

void ledGreen(bool on) {
  digitalWrite(LED_GREEN, LED_COMMON_ANODE ? !on : on);
}

void ledsOff() {
  ledRed(false);
  ledGreen(false);
}

void blinkRed(int times, int onMs = 120, int offMs = 100) {
  for (int i = 0; i < times; i++) {
    ledRed(true);
    ledGreen(false);
    delay(onMs);
    ledsOff();
    delay(offMs);
  }
}

void blinkGreen(int times, int onMs = 120, int offMs = 100) {
  for (int i = 0; i < times; i++) {
    ledGreen(true);
    ledRed(false);
    delay(onMs);
    ledsOff();
    delay(offMs);
  }
}

void blinkYellow(int times, int onMs = 120, int offMs = 100) {
  for (int i = 0; i < times; i++) {
    ledRed(true);
    ledGreen(true);
    delay(onMs);
    ledsOff();
    delay(offMs);
  }
}

// =====================
// Utility
// =====================
int readAvg(int pin, int samples = 12) {
  long total = 0;
  for (int i = 0; i < samples; i++) {
    total += analogRead(pin);
    delay(5);
  }
  return total / samples;
}

int mapClamped(int x, int in_min, int in_max, int out_min, int out_max) {
  if (in_max == in_min) return out_min;

  long value = (long)(x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;

  if (out_min < out_max) {
    if (value < out_min) value = out_min;
    if (value > out_max) value = out_max;
  } else {
    if (value > out_min) value = out_min;
    if (value < out_max) value = out_max;
  }

  return (int)value;
}

// =====================
// Power reading
// =====================
long readBatteryMv() {
  return 5000;
}

// =====================
// Power helpers
// =====================
void stopWiFi() {
  WiFi.mode(WIFI_OFF);
  esp_wifi_stop();
}

void goToSleep() {
  Serial.println("[SYS] Sleeping");
  ledsOff();

  LoRa.sleep();
  SPI.end();

  // Optional: reduce leakage on SPI pins during deep sleep
  pinMode(LORA_SS, INPUT);
  pinMode(LORA_SCK, INPUT);
  pinMode(LORA_MOSI, INPUT);
  pinMode(LORA_MISO, INPUT);
  pinMode(LORA_DIO0, INPUT);

  Serial.flush();
  esp_sleep_enable_timer_wakeup(SLEEP_INTERVAL_US);
  delay(50);
  esp_deep_sleep_start();
}

// =====================
// LoRa
// =====================
bool setupLoRa() {
  Serial.println("[LORA] Initializing...");
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(868E6)) {
    Serial.println("[LORA] Init failed!");
    return false;
  }

  LoRa.enableCrc();
  Serial.println("[LORA] Ready");
  return true;
}

void sendPlantPacket(uint16_t seq, long batteryMv, int soilRaw, int lightRaw, int tempCx10, int humPct) {
  String payload = "P," + String(seq) + "," + String(batteryMv) + "," +
                   String(soilRaw) + "," + String(lightRaw) + "," +
                   String(tempCx10) + "," + String(humPct);

  Serial.print("[LORA] TX: ");
  Serial.println(payload);

  LoRa.idle();
  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket(true);   // block until TX is done
}

bool waitForAck(uint16_t expectedSeq, unsigned long timeoutMs = 1500) {
  Serial.print("[ACK] Waiting for ACK for seq ");
  Serial.println(expectedSeq);

  delay(10);
  LoRa.receive();   // switch to RX mode

  unsigned long start = millis();

  while (millis() - start < timeoutMs) {
    int packetSize = LoRa.parsePacket();
    if (!packetSize) {
      delay(10);
      continue;
    }

    String raw = "";
    while (LoRa.available()) {
      raw += (char)LoRa.read();
    }

    Serial.print("[ACK] RX raw: ");
    Serial.println(raw);

    if (raw.startsWith("A,")) {
      uint16_t ackSeq = (uint16_t) raw.substring(2).toInt();
      if (ackSeq == expectedSeq) {
        Serial.print("[ACK] Received ACK for seq ");
        Serial.println(ackSeq);
        return true;
      }
    }
  }

  Serial.println("[ACK] Timeout waiting for ACK");
  return false;
}

// =====================
// Main
// =====================
void setup() {
  Serial.begin(115200);
  delay(300);

  stopWiFi();

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  ledsOff();

  pinMode(SOIL_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);

  dht.begin();
  delay(1000);

  Serial.println();
  Serial.println("==================================");
  esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  if (wakeCause == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Plant Transmitter Wake (from deep sleep)");
  } else {
    Serial.println("Plant Transmitter Boot (cold start/reset)");
  }
  Serial.println("==================================");

  Serial.print("[SEQ] Current stored seqCounter: ");
  Serial.println(seqCounter);

  // wake indicator
  blinkGreen(2, 60, 60);

  // ---- Read sensors
  int soilRaw = readAvg(SOIL_PIN);
  int lightRaw = readAvg(LDR_PIN);

  int moisturePct = mapClamped(soilRaw, SOIL_DRY, SOIL_WET, 0, 100);
  int lightPct    = mapClamped(lightRaw, LIGHT_DARK, LIGHT_BRIGHT, 0, 100);

  float temp = NAN;
  float hum = NAN;
  bool dhtOk = false;

  for (int i = 0; i < 3; i++) {
    temp = dht.readTemperature();
    hum  = dht.readHumidity();

    if (!isnan(temp) && !isnan(hum)) {
      dhtOk = true;
      break;
    }
    delay(200);
  }

  int tempCx10 = dhtOk ? (int)(temp * 10.0f + (temp >= 0 ? 0.5f : -0.5f)) : -999;
  int humPct   = dhtOk ? (int)(hum + 0.5f) : -1;

  long batteryMv = readBatteryMv();

  Serial.println("----");
  Serial.print("Soil raw: "); Serial.println(soilRaw);
  Serial.print("Moisture %: "); Serial.println(moisturePct);
  Serial.print("Light raw: "); Serial.println(lightRaw);
  Serial.print("Light %: "); Serial.println(lightPct);

  if (dhtOk) {
    Serial.print("Temp C: "); Serial.println(temp);
    Serial.print("Humidity: "); Serial.println(hum);
  } else {
    Serial.println("DHT11 error");
  }

  Serial.print("Battery mV: "); Serial.println(batteryMv);

  // ---- Status blink
  bool tooDry = moisturePct < MOISTURE_TOO_DRY_PCT;
  bool tooDark = lightPct < LIGHT_TOO_DARK_PCT;
  bool goodMoisture = moisturePct >= MOISTURE_GOOD_PCT;

  if (tooDry || tooDark) {
    Serial.println("Status: BAD");
    blinkRed(10, 80, 80);
  } else if (goodMoisture) {
    Serial.println("Status: GOOD/WET");
    blinkGreen(10, 80, 80);
  } else {
    Serial.println("Status: OK");
    blinkYellow(10, 60, 60);
  }

  // ---- Setup LoRa and transmit
  if (!setupLoRa()) {
    blinkRed(10, 40, 40);
    goToSleep();
  }

  uint16_t seq = nextSeq();
  Serial.print("[SEQ] Sending seq: ");
  Serial.println(seq);

  sendPlantPacket(seq, batteryMv, soilRaw, lightRaw, tempCx10, humPct);

  // IMPORTANT: sleep only AFTER ACK wait
  bool ackOk = waitForAck(seq, 1500);

  if (ackOk) {
    blinkGreen(3, 70, 70);
  } else {
    blinkRed(3, 70, 70);
  }

  goToSleep();
}

void loop() {
  // never reached
}