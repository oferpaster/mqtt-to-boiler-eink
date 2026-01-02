#include "epdif.h"
#include <SPI.h>

// Use one SPISettings config for the panel
static SPISettings EPD_SPI_SETTINGS(2000000, MSBFIRST, SPI_MODE0);

EpdIf::EpdIf() {}
EpdIf::~EpdIf() {}

void EpdIf::DigitalWrite(int pin, int value) {
    digitalWrite(pin, value);
}

int EpdIf::DigitalRead(int pin) {
    return digitalRead(pin);
}

void EpdIf::DelayMs(unsigned int delaytime) {
    delay(delaytime);
}

void EpdIf::SpiTransfer(unsigned char data) {
    // Start+end transaction per transfer (safe across re-init)
    SPI.beginTransaction(EPD_SPI_SETTINGS);
    digitalWrite(CS_PIN, LOW);
    SPI.transfer(data);
    digitalWrite(CS_PIN, HIGH);
    SPI.endTransaction();
}

int EpdIf::IfInit(void) {
    pinMode(CS_PIN, OUTPUT);
    pinMode(RST_PIN, OUTPUT);
    pinMode(DC_PIN, OUTPUT);

    // IMPORTANT: Busy can float -> use pullup for stability
    pinMode(BUSY_PIN, INPUT_PULLUP);

    // Panel power enable
    pinMode(PWR_PIN, OUTPUT);

    // DO NOT leave beginTransaction open here
    // SPI pins: if you already do SPI.begin(...) in main.cpp with explicit pins,
    // SPI.begin() here is still fine (idempotent).
    SPI.begin();

    // Default lines
    digitalWrite(CS_PIN, HIGH);
    digitalWrite(DC_PIN, HIGH);
    digitalWrite(RST_PIN, HIGH);

    return 0;
}
