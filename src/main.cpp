#include <Arduino.h>
#include <SPI.h>

// ESP32 VSPI pins (fixed)
#define DIN_PIN 23 // MOSI
#define CLK_PIN 18 // CLK
#define CS_PIN 5   // CS/SS
#define NUM_DISPLAYS 4

// MAX7219 registers
#define REG_DECODE 0x09
#define REG_INTENSITY 0x0A
#define REG_SCAN_LIMIT 0x0B
#define REG_SHUTDOWN 0x0C
#define REG_TEST 0x0F

uint8_t framebuffer[4][8];

// Forward declarations
void sendCommandToAll(uint8_t reg, uint8_t data);
void sendCommand(int chip, uint8_t reg, uint8_t data);
void updateDisplay();
void displayChar(int chip, char c);
void testPattern();

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\nStarting MAX7219 Initialization...");

  // Initialize SPI - ESP32 VSPI
  SPI.begin(CLK_PIN, -1, DIN_PIN, CS_PIN);
  SPI.setFrequency(1000000); // 1 MHz
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);

  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);

  Serial.println("SPI initialized");
  delay(100);

  // Initialize all MAX7219 chips - with delays between commands
  Serial.println("Sending Shutdown OFF (wake up)...");
  sendCommandToAll(REG_SHUTDOWN, 0x01);
  delay(10);

  Serial.println("Setting Scan Limit...");
  sendCommandToAll(REG_SCAN_LIMIT, 0x07);
  delay(10);

  Serial.println("Setting Decode Mode...");
  sendCommandToAll(REG_DECODE, 0x00);
  delay(10);

  Serial.println("Setting Intensity...");
  sendCommandToAll(REG_INTENSITY, 0x0F);
  delay(10);

  // Clear all displays with explicit row data
  Serial.println("Clearing all displays...");
  for (int row = 1; row <= 8; row++)
  {
    digitalWrite(CS_PIN, LOW);
    for (int chip = 0; chip < NUM_DISPLAYS; chip++)
    {
      SPI.write(row);
      SPI.write(0x00); // Clear this row
    }
    digitalWrite(CS_PIN, HIGH);
    delayMicroseconds(100);
  }

  delay(100);
  memset(framebuffer, 0, sizeof(framebuffer));

  Serial.println("MAX7219 4-Display System Initialized (Hardware SPI, No Library)");
  Serial.println("Ready to display!");
}

void loop()
{
  // Simple test - turn on row 1 of all displays
  Serial.println("\n=== TEST 1: Row 1 pattern ===");
  memset(framebuffer, 0, sizeof(framebuffer));
  for (int chip = 0; chip < NUM_DISPLAYS; chip++)
  {
    framebuffer[chip][0] = 0xFF; // Row 1, all LEDs on
  }
  for (int i = 0; i < 50; i++)
  {
    updateDisplay();
    delay(50);
  }

  // Display "HELO"
  Serial.println("\n=== Display: HELO ===");
  displayChar(0, 'H');
  displayChar(1, 'E');
  displayChar(2, 'L');
  displayChar(3, 'O');
  for (int i = 0; i < 30; i++)
  {
    updateDisplay();
    delay(100);
  }

  // Display "CLOK"
  Serial.println("\n=== Display: CLOK ===");
  displayChar(0, 'C');
  displayChar(1, 'L');
  displayChar(2, 'O');
  displayChar(3, 'K');
  for (int i = 0; i < 30; i++)
  {
    updateDisplay();
    delay(100);
  }

  // Display "TEST"
  Serial.println("\n=== Display: TEST ===");
  displayChar(0, 'T');
  displayChar(1, 'E');
  displayChar(2, 'S');
  displayChar(3, 'T');
  for (int i = 0; i < 30; i++)
  {
    updateDisplay();
    delay(100);
  }

  // Test pattern - all LEDs on
  Serial.println("\n=== Test Pattern: All LEDs ON ===");
  for (int chip = 0; chip < NUM_DISPLAYS; chip++)
  {
    for (int row = 0; row < 8; row++)
    {
      framebuffer[chip][row] = 0xFF;
    }
  }
  for (int i = 0; i < 20; i++)
  {
    updateDisplay();
    delay(100);
  }

  // Clear
  Serial.println("\n=== Clearing ===");
  memset(framebuffer, 0, sizeof(framebuffer));
  for (int i = 0; i < 10; i++)
  {
    updateDisplay();
    delay(100);
  }
}

void sendCommandToAll(uint8_t reg, uint8_t data)
{
  digitalWrite(CS_PIN, LOW);
  delayMicroseconds(1);

  for (int i = 0; i < NUM_DISPLAYS; i++)
  {
    SPI.write(reg);
    SPI.write(data);
    delayMicroseconds(1);
  }

  delayMicroseconds(1);
  digitalWrite(CS_PIN, HIGH);
  delayMicroseconds(10);
}

void sendCommand(int chip, uint8_t reg, uint8_t data)
{
  digitalWrite(CS_PIN, LOW);
  for (int i = 0; i < NUM_DISPLAYS; i++)
  {
    if (i == chip)
    {
      SPI.write(reg);
      SPI.write(data);
    }
    else
    {
      SPI.write(0);
      SPI.write(0);
    }
  }
  digitalWrite(CS_PIN, HIGH);
  delayMicroseconds(10);
}

void updateDisplay()
{
  // Send row data for all 8 rows
  for (int row = 1; row <= 8; row++)
  {
    digitalWrite(CS_PIN, LOW);
    delayMicroseconds(1);

    // Send to all 4 chips in daisy chain
    for (int chip = 0; chip < NUM_DISPLAYS; chip++)
    {
      SPI.write(row);                        // Register address (row 1-8)
      SPI.write(framebuffer[chip][row - 1]); // Row data
    }

    delayMicroseconds(1);
    digitalWrite(CS_PIN, HIGH);
    delayMicroseconds(10); // Latch delay
  }
}

// 5x7 character patterns (7 rows each)
byte patterns[26][7] = {
    // A
    {0x3C, 0x42, 0x81, 0xFF, 0x81, 0x81, 0x00},
    // B
    {0xFE, 0x81, 0x81, 0xFE, 0x81, 0x81, 0xFE},
    // C
    {0x7E, 0x81, 0x80, 0x80, 0x80, 0x81, 0x7E},
    // D
    {0xFE, 0x81, 0x81, 0x81, 0x81, 0x81, 0xFE},
    // E
    {0xFF, 0x80, 0x80, 0xFE, 0x80, 0x80, 0xFF},
    // F
    {0xFF, 0x80, 0x80, 0xFE, 0x80, 0x80, 0x80},
    // G
    {0x7E, 0x81, 0x80, 0x8F, 0x81, 0x81, 0x7E},
    // H
    {0x81, 0x81, 0x81, 0xFF, 0x81, 0x81, 0x81},
    // I
    {0xFF, 0x18, 0x18, 0x18, 0x18, 0x18, 0xFF},
    // J
    {0x0F, 0x08, 0x08, 0x08, 0x88, 0x88, 0x70},
    // K
    {0x81, 0x42, 0x24, 0x18, 0x24, 0x42, 0x81},
    // L
    {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xFF},
    // M
    {0x81, 0xC3, 0xA5, 0x99, 0x81, 0x81, 0x81},
    // N
    {0x81, 0xC1, 0xA1, 0x91, 0x89, 0x85, 0x83},
    // O
    {0x3C, 0x42, 0x81, 0x81, 0x81, 0x42, 0x3C},
    // P
    {0xFE, 0x81, 0x81, 0xFE, 0x80, 0x80, 0x80},
    // Q
    {0x3C, 0x42, 0x81, 0x81, 0x89, 0x42, 0x3C},
    // R
    {0xFE, 0x81, 0x81, 0xFE, 0x88, 0x84, 0x82},
    // S
    {0x7E, 0x80, 0x80, 0x7E, 0x01, 0x01, 0xFE},
    // T
    {0xFF, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18},
    // U
    {0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7E},
    // V
    {0x81, 0x81, 0x81, 0x42, 0x42, 0x24, 0x18},
    // W
    {0x81, 0x81, 0x81, 0x99, 0xA5, 0xC3, 0x81},
    // X
    {0x81, 0x42, 0x24, 0x18, 0x24, 0x42, 0x81},
    // Y
    {0x81, 0x42, 0x24, 0x18, 0x18, 0x18, 0x18},
    // Z
    {0xFF, 0x02, 0x04, 0x08, 0x10, 0x20, 0xFF}};

void displayChar(int chip, char c)
{
  // Clear the display for this chip
  for (int row = 0; row < 8; row++)
  {
    framebuffer[chip][row] = 0;
  }

  // Display character (place in rows 1-7)
  if (c >= 'A' && c <= 'Z')
  {
    int idx = c - 'A';
    for (int row = 0; row < 7; row++)
    {
      framebuffer[chip][row + 1] = patterns[idx][row];
    }
  }
}

void testPattern()
{
  // Light all LEDs
  for (int chip = 0; chip < NUM_DISPLAYS; chip++)
  {
    for (int row = 0; row < 8; row++)
    {
      framebuffer[chip][row] = 0xFF; // All LEDs on
    }
  }
  updateDisplay();
  delay(1000);

  // Clear all
  memset(framebuffer, 0, sizeof(framebuffer));
  updateDisplay();
  delay(500);
}
