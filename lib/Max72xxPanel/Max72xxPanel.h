#ifndef Max72xxPanel_h
#define Max72xxPanel_h

#include "Arduino.h"
#include "SPI.h"
#include "Adafruit_GFX.h"

class Max72xxPanel : public Adafruit_GFX
{
public:
    Max72xxPanel(int csPin, int hDisplays, int vDisplays);

    void setIntensity(int intensity);
    void setRotation(int display, int rotation);
    void fillScreen(uint16_t color);
    void write();
    void drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color, uint16_t bg, uint8_t size);
    virtual void drawPixel(int16_t x, int16_t y, uint16_t color);

private:
    int csPin;
    int numberOfHorizontalDisplays;
    int numberOfVerticalDisplays;
    uint8_t *displayBuffer;
    int bufferSize;
    int intensity;
    uint8_t rotations[8];

    void spiWrite(uint8_t reg, uint8_t data);
    void initializeDisplay(uint8_t reg, uint8_t val);
};

#endif
