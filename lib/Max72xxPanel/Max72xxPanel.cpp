#include "Max72xxPanel.h"

Max72xxPanel::Max72xxPanel(int csPin, int hDisplays, int vDisplays)
    : Adafruit_GFX(hDisplays * 8, vDisplays * 8),
      csPin(csPin),
      numberOfHorizontalDisplays(hDisplays),
      numberOfVerticalDisplays(vDisplays),
      intensity(8)
{
    Serial.println("Max72xxPanel: Constructor starting");
    Serial.print("Displays: ");
    Serial.print(hDisplays);
    Serial.print("x");
    Serial.println(vDisplays);

    bufferSize = hDisplays * vDisplays * 8;
    displayBuffer = (uint8_t *)malloc(bufferSize);
    memset(displayBuffer, 0, bufferSize);
    memset(rotations, 0, 8);

    pinMode(csPin, OUTPUT);
    digitalWrite(csPin, HIGH);
    SPI.begin();
    SPI.setClockDivider(SPI_CLOCK_DIV32); // Slower speed for reliability
    SPI.setDataMode(SPI_MODE0);

    Serial.println("Max72xxPanel: SPI initialized, waiting 500ms");
    delay(500);

    // Send shutdown OFF to all displays
    Serial.println("Max72xxPanel: Sending shutdown OFF command");
    digitalWrite(csPin, LOW);
    delay(10);
    for (int i = 0; i < numberOfHorizontalDisplays * numberOfVerticalDisplays; i++)
    {
        SPI.transfer(0x0C); // Shutdown register
        SPI.transfer(0x00); // Shutdown mode
    }
    digitalWrite(csPin, HIGH);
    delay(100);

    // Reset and initialize all registers
    Serial.println("Max72xxPanel: Initializing registers");
    uint8_t initRegs[] = {0x0F, 0x09, 0x0B, 0x0A, 0x0C};
    uint8_t initVals[] = {0x00, 0x00, 0x07, 0x08, 0x01};

    for (int reg_idx = 0; reg_idx < 5; reg_idx++)
    {
        Serial.print("Max72xxPanel: Register 0x");
        Serial.print(initRegs[reg_idx], HEX);
        Serial.print(" = 0x");
        Serial.println(initVals[reg_idx], HEX);

        digitalWrite(csPin, LOW);
        delay(5);
        for (int i = 0; i < numberOfHorizontalDisplays * numberOfVerticalDisplays; i++)
        {
            SPI.transfer(initRegs[reg_idx]);
            delay(1);
            SPI.transfer(initVals[reg_idx]);
            delay(1);
        }
        digitalWrite(csPin, HIGH);
        delay(100);
    }

    // Clear all row data
    Serial.println("Max72xxPanel: Clearing all row data");
    for (int row = 1; row <= 8; row++)
    {
        digitalWrite(csPin, LOW);
        delay(5);
        for (int i = 0; i < numberOfHorizontalDisplays * numberOfVerticalDisplays; i++)
        {
            SPI.transfer(row);
            delay(1);
            SPI.transfer(0x00);
            delay(1);
        }
        digitalWrite(csPin, HIGH);
        delay(100);
    }

    Serial.println("Max72xxPanel: Constructor complete!");
}

void Max72xxPanel::initializeDisplay(uint8_t reg, uint8_t val)
{
    digitalWrite(csPin, LOW);
    delayMicroseconds(10);
    for (int i = 0; i < numberOfHorizontalDisplays * numberOfVerticalDisplays; i++)
    {
        SPI.transfer(reg);
        delayMicroseconds(1);
        SPI.transfer(val);
        delayMicroseconds(1);
    }
    digitalWrite(csPin, HIGH);
    delayMicroseconds(100);
}

void Max72xxPanel::setIntensity(int value)
{
    intensity = constrain(value, 0, 15);
    initializeDisplay(0x0A, intensity);
}

void Max72xxPanel::setRotation(int display, int rotation)
{
    if (display < numberOfHorizontalDisplays * numberOfVerticalDisplays)
    {
        rotations[display] = rotation;
    }
}

void Max72xxPanel::fillScreen(uint16_t color)
{
    if (color == 0)
    {
        memset(displayBuffer, 0, bufferSize);
    }
    else
    {
        memset(displayBuffer, 0xFF, bufferSize);
    }
}

void Max72xxPanel::spiWrite(uint8_t reg, uint8_t data)
{
    initializeDisplay(reg, data);
}

void Max72xxPanel::write()
{
    static int writeCount = 0;
    if (writeCount++ % 10 == 0)
    {
        Serial.print("Max72xxPanel::write() - Buffer: ");
        for (int i = 0; i < bufferSize && i < 16; i++)
        {
            Serial.print(displayBuffer[i], HEX);
            Serial.print(" ");
        }
        Serial.println();
    }

    for (int row = 1; row <= 8; row++)
    {
        digitalWrite(csPin, LOW);
        delay(5);
        for (int display = 0; display < numberOfHorizontalDisplays * numberOfVerticalDisplays; display++)
        {
            SPI.transfer(row);
            delay(1);
            SPI.transfer(displayBuffer[display * 8 + (row - 1)]);
            delay(1);
        }
        digitalWrite(csPin, HIGH);
        delay(10);
    }
}

void Max72xxPanel::drawPixel(int16_t x, int16_t y, uint16_t color)
{
    if ((x < 0) || (x >= width()) || (y < 0) || (y >= height()))
        return;

    int displayX = x / 8;
    int displayY = y / 8;
    if (displayX >= numberOfHorizontalDisplays || displayY >= numberOfVerticalDisplays)
        return;

    int bufferIndex = (displayY * numberOfHorizontalDisplays + displayX) * 8 + (y % 8);
    int bitPosition = 7 - (x % 8);

    if (color)
    {
        displayBuffer[bufferIndex] |= (1 << bitPosition);
    }
    else
    {
        displayBuffer[bufferIndex] &= ~(1 << bitPosition);
    }
}

void Max72xxPanel::drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color, uint16_t bg, uint8_t size)
{
    Adafruit_GFX::drawChar(x, y, c, color, bg, size);
}
