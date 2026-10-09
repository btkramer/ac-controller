// Display bring-up for the likely Sunton/GUITION ESP32-4848S040: pins and init
// sequence taken from GFX Library's ESP32_4848S040_86BOX_GUITION config.
// Draws RGB bars + text, backlight on GPIO38, and logs GT911 touches.
#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>

#define GFX_BL 38
Arduino_DataBus *bus = new Arduino_SWSPI(GFX_NOT_DEFINED, 39, 48, 47, GFX_NOT_DEFINED);
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    18, 17, 16, 21,
    11, 12, 13, 14, 0,
    8, 20, 3, 46, 9, 10,
    4, 5, 6, 7, 15,
    1, 10, 8, 50, 1, 10, 8, 20,
    0, 12000000, false, 0, 0, 0);
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    480, 480, rgbpanel, 0, true, bus, GFX_NOT_DEFINED,
    st7701_type9_init_operations, sizeof(st7701_type9_init_operations));

static bool gt911Read(uint16_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(0x5D);
  Wire.write(reg >> 8); Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom((uint8_t)0x5D, n);
  for (uint8_t i = 0; i < n && Wire.available(); i++) buf[i] = Wire.read();
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n== DISPTEST ==");
  if (!gfx->begin()) Serial.println("gfx->begin() FAILED");
  gfx->fillScreen(BLACK);
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
  const uint16_t cols[] = {RED, GREEN, BLUE, WHITE};
  for (int i = 0; i < 4; i++) gfx->fillRect(i * 120, 0, 120, 240, cols[i]);
  gfx->setTextColor(WHITE); gfx->setTextSize(4);
  gfx->setCursor(20, 300); gfx->println("ac-controller");
  gfx->setCursor(20, 350); gfx->println("display OK?");
  gfx->setCursor(20, 400); gfx->println("touch the screen");
  Wire.begin(19, 45, 400000);
  Serial.println("display drawn; polling GT911");
}

void loop() {
  uint8_t st = 0, p[4];
  if (gt911Read(0x814E, &st, 1) && (st & 0x80)) {
    uint8_t n = st & 0x0F;
    if (n && gt911Read(0x8150, p, 4)) {
      uint16_t x = p[0] | (p[1] << 8), y = p[2] | (p[3] << 8);
      Serial.printf("touch x=%u y=%u\n", x, y);
      gfx->fillCircle(x, y, 8, YELLOW);
    }
    uint8_t z = 0;
    Wire.beginTransmission(0x5D); Wire.write(0x81); Wire.write(0x4E); Wire.write(z); Wire.endTransmission();
  }
  delay(20);
}
