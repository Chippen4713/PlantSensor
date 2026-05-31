#include "esp_wifi.h"
#include "esp_sleep.h"

// =====================
// Config
// =====================
#define SLEEP_INTERVAL_US  (15ULL * 60 * 1000000)  // 15 minutes between transmissions

// =====================
// LoRa pins
// =====================
#define LORA_SS    6
#define LORA_RST   5
#define LORA_MOSI  9
#define LORA_MISO  8
#define LORA_SCK   7

// =====================
// LoRa register map
// =====================
#define REG_FIFO                 0x00
#define REG_OP_MODE              0x01
#define REG_FRF_MSB              0x06
#define REG_FRF_MID              0x07
#define REG_FRF_LSB              0x08
#define REG_PA_CONFIG            0x09
#define REG_FIFO_ADDR_PTR        0x0D
#define REG_FIFO_TX_BASE_ADDR    0x0E
#define REG_FIFO_RX_BASE_ADDR    0x0F
#define REG_IRQ_FLAGS            0x12
#define REG_MODEM_CONFIG_1       0x1D
#define REG_MODEM_CONFIG_2       0x1E
#define REG_PREAMBLE_MSB         0x20
#define REG_PREAMBLE_LSB         0x21
#define REG_PAYLOAD_LENGTH       0x22
#define REG_DIO_MAPPING_1        0x40
#define REG_VERSION              0x42

// =====================
// LoRa mode flags
// =====================
#define MODE_LONG_RANGE_MODE     0x80
#define MODE_SLEEP               0x00
#define MODE_STDBY               0x01
#define MODE_TX                  0x03

#define PA_BOOST                 0x80
#define IRQ_TX_DONE_MASK         0x08

// =====================
// Soft SPI
// =====================
void spiDelay() {
  delayMicroseconds(2);
}

byte softSPITransfer(byte data) {
  byte received = 0;

  for (int i = 0; i < 8; i++) {
    digitalWrite(LORA_MOSI, (data & 0x80) ? HIGH : LOW);
    data <<= 1;

    digitalWrite(LORA_SCK, HIGH);
    spiDelay();

    received <<= 1;
    if (digitalRead(LORA_MISO)) {
      received |= 1;
    }

    digitalWrite(LORA_SCK, LOW);
    spiDelay();
  }

  return received;
}

// =====================
// LoRa register access
// =====================
void writeRegister(byte addr, byte value) {
  digitalWrite(LORA_SS, LOW);
  spiDelay();
  softSPITransfer(addr | 0x80);
  softSPITransfer(value);
  spiDelay();
  digitalWrite(LORA_SS, HIGH);
}

byte readRegister(byte addr) {
  digitalWrite(LORA_SS, LOW);
  spiDelay();
  softSPITransfer(addr & 0x7F);
  byte val = softSPITransfer(0x00);
  spiDelay();
  digitalWrite(LORA_SS, HIGH);
  return val;
}

// =====================
// LoRa control
// =====================
void loraStandby() {
  writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY);
}

void loraSleep() {
  writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_SLEEP);
}

bool loraInit() {
  pinMode(LORA_SS, OUTPUT);
  pinMode(LORA_RST, OUTPUT);
  pinMode(LORA_MOSI, OUTPUT);
  pinMode(LORA_MISO, INPUT);
  pinMode(LORA_SCK, OUTPUT);

  digitalWrite(LORA_SS, HIGH);
  digitalWrite(LORA_SCK, LOW);
  digitalWrite(LORA_MOSI, LOW);

  digitalWrite(LORA_RST, LOW);
  delay(20);
  digitalWrite(LORA_RST, HIGH);
  delay(100);

  byte version = readRegister(REG_VERSION);
  Serial.print("[LoRa] version: 0x");
  Serial.println(version, HEX);

  if (version != 0x12) {
    return false;
  }

  writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_SLEEP);
  delay(10);

  writeRegister(REG_FIFO_TX_BASE_ADDR, 0x00);
  writeRegister(REG_FIFO_RX_BASE_ADDR, 0x00);
  writeRegister(REG_FIFO_ADDR_PTR, 0x00);

  // 868 MHz
  writeRegister(REG_FRF_MSB, 0xD9);
  writeRegister(REG_FRF_MID, 0x00);
  writeRegister(REG_FRF_LSB, 0x00);

  writeRegister(REG_PA_CONFIG, PA_BOOST | 0x0C);
  writeRegister(REG_MODEM_CONFIG_1, 0x72);
  writeRegister(REG_MODEM_CONFIG_2, 0x74);
  writeRegister(REG_PREAMBLE_MSB, 0x00);
  writeRegister(REG_PREAMBLE_LSB, 0x08);
  writeRegister(REG_DIO_MAPPING_1, 0x00);

  writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY);
  delay(10);

  return true;
}

void sendRaw(const char* msg, byte len) {
  loraStandby();
  writeRegister(REG_FIFO_ADDR_PTR, 0x00);

  for (byte i = 0; i < len; i++) {
    writeRegister(REG_FIFO, msg[i]);
  }

  writeRegister(REG_PAYLOAD_LENGTH, len);
  writeRegister(REG_IRQ_FLAGS, 0xFF);
  writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_TX);

  unsigned long start = millis();
  while ((readRegister(REG_IRQ_FLAGS) & IRQ_TX_DONE_MASK) == 0) {
    if (millis() - start > 3000) {
      Serial.println("[LoRa] TX timeout");
      break;
    }
  }

  writeRegister(REG_IRQ_FLAGS, IRQ_TX_DONE_MASK);
  loraSleep();
}

// =====================
// Main
// =====================
void setup() {
  // Disable WiFi radio — not used, saves ~10 mA
  esp_wifi_stop();

  Serial.begin(115200);

  if (!loraInit()) {
    Serial.println("[LoRa] init failed, sleeping");
    esp_sleep_enable_timer_wakeup(SLEEP_INTERVAL_US);
    esp_deep_sleep_start();
  }

  Serial.println("[LoRa] init OK, sending");
  const char* msg = "hello";
  sendRaw(msg, 5);
  Serial.println("[LoRa] sent, going to sleep");

  esp_sleep_enable_timer_wakeup(SLEEP_INTERVAL_US);
  esp_deep_sleep_start();
}

void loop() {
  // Never reached — device deep sleeps between transmissions
}
