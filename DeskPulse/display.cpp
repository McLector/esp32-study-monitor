#include "display.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static Adafruit_SSD1306 oled(128, 64, &Wire, -1);  // -1: no reset pin
static bool ready = false;
static int8_t lastDim = -1, lastOff = -1;  // only send panel commands on change

bool displayInit() {
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);  // a full 1 KB frame still takes about 25 ms
  // begin() doesn't check that anything answers, so probe the address first:
  // a missing or unplugged OLED must not look like a working one.
  Wire.beginTransmission(OLED_ADDR);
  if (Wire.endTransmission() != 0) return ready = false;
  ready = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!ready) return false;
  oled.setTextColor(SSD1306_WHITE);
  oled.clearDisplay();
  oled.display();
  return true;
}

static void printRight(int16_t y, const char* text) {
  oled.setCursor(128 - 6 * (int16_t)strlen(text), y);  // 6 px per char at size 1
  oled.print(text);
}

void displaySplash(const char* line) {
  if (!ready) return;
  oled.clearDisplay();
  oled.setTextSize(2);
  oled.setCursor(0, 8);  oled.print("DeskPulse");
  oled.setCursor(0, 28); oled.print("2.0");
  oled.setTextSize(1);
  oled.setCursor(0, 54); oled.print(line);
  oled.display();
}

void displayDraw(const Screen& s) {
  if (!ready) return;
  if (s.off != lastOff) {
    oled.ssd1306_command(s.off ? SSD1306_DISPLAYOFF : SSD1306_DISPLAYON);
    lastOff = s.off;
  }
  if (s.off) return;
  if (s.dim != lastDim) {
    oled.dim(s.dim);
    lastDim = s.dim;
  }

  oled.clearDisplay();
  if (s.eyeRest) {
    oled.setTextSize(2); oled.setCursor(0, 0);  oled.print("EYE REST");
    oled.setTextSize(1); oled.setCursor(0, 22); oled.print("Look 20 ft (6 m) away");
    oled.setTextSize(3); oled.setCursor(46, 38); oled.printf("%2u", s.eyeLeft);
    oled.display();
    return;
  }

  oled.setTextSize(2); oled.setCursor(0, 0);  oled.print(s.title);
  oled.setTextSize(1); oled.setCursor(0, 20); oled.print(s.timers);
  oled.drawRect(0, 31, 128, 7, SSD1306_WHITE);
  oled.fillRect(1, 32, s.barPx, 5, SSD1306_WHITE);
  oled.setCursor(0, 42); oled.print(s.env);
  printRight(42, s.dist);
  oled.setCursor(0, 54); oled.print(s.focus);
  printRight(54, s.link);
  oled.display();
}
