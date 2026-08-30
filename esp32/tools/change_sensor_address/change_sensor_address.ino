#include <Arduino.h>
#include <ModbusMaster.h>

// Connect ONLY ONE sensor while running this utility.
static constexpr int RX_PIN = 16;
static constexpr int TX_PIN = 17;
static constexpr int DE_RE_PIN = 4;

static constexpr uint32_t BAUD = 4800;
static constexpr uint8_t OLD_ID = 1;
static constexpr uint8_t NEW_ID = 2;

// Common VEM SEE / generic probe address register.
static constexpr uint16_t ADDRESS_REGISTER = 0x07D0;

HardwareSerial rs485(2);
ModbusMaster node;

void preTx() {
  digitalWrite(DE_RE_PIN, HIGH);
  delayMicroseconds(200);
}

void postTx() {
  rs485.flush();
  delayMicroseconds(200);
  digitalWrite(DE_RE_PIN, LOW);
}

void setup() {
  Serial.begin(115200);
  pinMode(DE_RE_PIN, OUTPUT);
  digitalWrite(DE_RE_PIN, LOW);

  rs485.begin(BAUD, SERIAL_8N1, RX_PIN, TX_PIN);
  node.begin(OLD_ID, rs485);
  node.preTransmission(preTx);
  node.postTransmission(postTx);

  delay(1000);
  Serial.printf("Changing Modbus address %u -> %u\n", OLD_ID, NEW_ID);

  // Many matching sensors document address changes using FC 0x10.
  // ModbusMaster writeSingleRegister uses FC 0x06. If your exact sensor
  // accepts only FC 0x10, use the raw command shown in your sensor manual.
  uint8_t result = node.writeSingleRegister(ADDRESS_REGISTER, NEW_ID);

  if (result == node.ku8MBSuccess) {
    Serial.println("Write accepted. Power-cycle the sensor, then test at the new address.");
  } else {
    Serial.printf("Modbus error: 0x%02X\n", result);
    Serial.println("Your sensor may require function 0x10 for this register.");
  }
}

void loop() {}
