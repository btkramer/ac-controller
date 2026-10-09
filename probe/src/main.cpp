// Prints chip info, then brute-force scans I2C on every plausible GPIO pair
// to find the touch controller / IO expander and which pins they sit on.
#include <Arduino.h>
#include <Wire.h>

static const int pins[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,38,39,40,41,42,45,46,47,48};
// GT911 5D/14, FT6x36 38, CST816 15, CST3xx 1A, TCA9554/PCF8574 20-27, PCF85063 51, QMI8658 6B/6A, PCA9554 38+, AXP 34, DS3231/MPU 68
static const uint8_t likely[] = {0x5D,0x14,0x38,0x15,0x1A,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x51,0x6B,0x6A,0x34,0x68,0x3C,0x48};
static const int N = sizeof(pins) / sizeof(pins[0]);

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.printf("\n== PROBE ==\nchip %s rev %d, %d MHz\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz());
  Serial.printf("flash %u MB, psram %u MB\n", ESP.getFlashChipSize() >> 20, ESP.getPsramSize() >> 20);
  Serial.printf("mac %012llx\n", ESP.getEfuseMac());
  Serial.println("I2C scan (sda,scl -> addrs):");
  for (int a = 0; a < N; a++) { Serial.printf("sda=%d...\n", pins[a]); for (int b = 0; b < N; b++) {
    if (a == b) continue;
    Wire.begin(pins[a], pins[b], 400000);
    Wire.setTimeOut(2);
    String hits;
    for (uint8_t addr : likely) {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0) hits += String(" 0x") + String(addr, HEX);
    }
    Wire.end();
    if (hits.length()) Serial.printf("  sda=%d scl=%d ->%s\n", pins[a], pins[b], hits.c_str());
  }
  }
  Serial.println("== DONE ==");
}

void loop() { delay(1000); }
