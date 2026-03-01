#pragma once

#include "DisplayDriver.h"
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <helpers/RefCountedDigitalPin.h>

#ifndef DISPLAY_LOGICAL_WIDTH
  #define DISPLAY_LOGICAL_WIDTH 128
#endif

#ifndef DISPLAY_LOGICAL_HEIGHT
  #define DISPLAY_LOGICAL_HEIGHT 64
#endif

class ST7789LCDDisplay : public DisplayDriver {
  #if defined(LILYGO_TDECK) || defined(HELTEC_LORA_V4_TFT)
    SPIClass displaySPI;
  #endif
  Adafruit_ST7789 display;
  GFXcanvas16* _canvas;
  bool _isOn;
  uint16_t _color;
  RefCountedDigitalPin* _peripher_power;

  bool i2c_probe(TwoWire& wire, uint8_t addr);
public:
#ifdef USE_PIN_TFT
  ST7789LCDDisplay(RefCountedDigitalPin* peripher_power=NULL) : DisplayDriver(DISPLAY_LOGICAL_WIDTH, DISPLAY_LOGICAL_HEIGHT), 
      display(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_SDA, PIN_TFT_SCL, PIN_TFT_RST),
      _peripher_power(peripher_power)
  {
    _isOn = false;
  }
#elif defined(LILYGO_TDECK) || defined(HELTEC_LORA_V4_TFT)
  ST7789LCDDisplay(RefCountedDigitalPin* peripher_power=NULL) : DisplayDriver(DISPLAY_LOGICAL_WIDTH, DISPLAY_LOGICAL_HEIGHT),
      displaySPI(HSPI),
      display(&displaySPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST),
      _peripher_power(peripher_power)
  {
    _isOn = false;
  }
#else
  ST7789LCDDisplay(RefCountedDigitalPin* peripher_power=NULL) : DisplayDriver(DISPLAY_LOGICAL_WIDTH, DISPLAY_LOGICAL_HEIGHT), 
      display(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST),
      _peripher_power(peripher_power)
  {
    _isOn = false;
  }
#endif
  bool begin();

  bool isOn() override { return _isOn; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(Color bkg = DARK) override;
  void setTextSize(int sz) override;
  void setColor(Color c) override;
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawRoundRect(int x, int y, int w, int h, int r) override;
  void fillRoundRect(int x, int y, int w, int h, int r) override;
  void drawLine(int x0, int y0, int x1, int y1) override;
  void drawFastHLine(int x, int y, int w) override;
  void drawFastVLine(int x, int y, int h) override;
  void drawCircle(int x, int y, int r) override;
  void fillCircle(int x, int y, int r) override;
  void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2) override;
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override;
};
