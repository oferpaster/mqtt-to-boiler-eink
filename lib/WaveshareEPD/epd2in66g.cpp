#include <stdlib.h>
#include "epd2in66g.h"
#include "epdif.h"

Epd::~Epd() {}

Epd::Epd() {
    reset_pin = RST_PIN;
    dc_pin = DC_PIN;
    cs_pin = CS_PIN;
    busy_pin = BUSY_PIN;
    WIDTH = EPD_WIDTH;
    HEIGHT = EPD_HEIGHT;
}

int Epd::Init() {
    if (IfInit() != 0) {
        return -1;
    }

    // ===== CRITICAL FIX: power-cycle the panel using PWR pin =====
    // Many Waveshare boards require toggling PWR to wake reliably after POWER_OFF.
    DigitalWrite(PWR_PIN, LOW);
    DelayMs(80);
    DigitalWrite(PWR_PIN, HIGH);
    DelayMs(80);

    // Reset + wait idle
    Reset();
    ReadBusyH();

    SendCommand(0x4D);
    SendData(0x78);

    SendCommand(0x00);  // PSR
    SendData(0x0F);
    SendData(0x29);

    SendCommand(0x01);  // PWRR
    SendData(0x07);
    SendData(0x00);

    SendCommand(0x03);  // POFS
    SendData(0x10);
    SendData(0x54);
    SendData(0x44);

    SendCommand(0x06);  // BTST_P
    SendData(0x05);
    SendData(0x00);
    SendData(0x3F);
    SendData(0x0A);
    SendData(0x25);
    SendData(0x12);
    SendData(0x1A);

    SendCommand(0x50);  // CDI
    SendData(0x37);

    SendCommand(0x60);  // TCON
    SendData(0x02);
    SendData(0x02);

    SendCommand(0x61);  // TRES
    SendData(WIDTH / 256);
    SendData(WIDTH % 256);
    SendData(HEIGHT / 256);
    SendData(HEIGHT % 256);

    SendCommand(0xE7);
    SendData(0x1C);

    SendCommand(0xE3);
    SendData(0x22);

    SendCommand(0xB4);
    SendData(0xD0);
    SendCommand(0xB5);
    SendData(0x03);

    SendCommand(0xE9);
    SendData(0x01);

    SendCommand(0x30);
    SendData(0x08);

    // POWER_ON
    SendCommand(0x04);
    ReadBusyH();

    return 0;
}

void Epd::SendCommand(unsigned char command) {
    DigitalWrite(dc_pin, LOW);
    SpiTransfer(command);
}

void Epd::SendData(unsigned char data) {
    DigitalWrite(dc_pin, HIGH);
    SpiTransfer(data);
}

void Epd::ReadBusyH(void) {
    Serial.print("e-Paper busy H\r\n ");
    while (DigitalRead(busy_pin) == LOW) { // LOW busy, HIGH idle
        DelayMs(5);
    }
    Serial.print("e-Paper busy release H\r\n ");
}

void Epd::ReadBusyL(void) {
    Serial.print("e-Paper busy L\r\n ");
    while (DigitalRead(busy_pin) == HIGH) { // HIGH busy, LOW idle
        DelayMs(5);
    }
    Serial.print("e-Paper busy release L\r\n ");
}

void Epd::Reset(void) {
    DigitalWrite(reset_pin, HIGH);
    DelayMs(20);
    DigitalWrite(reset_pin, LOW);
    DelayMs(2);
    DigitalWrite(reset_pin, HIGH);
    DelayMs(20);
}

void Epd::TurnOnDisplay(void) {
    SendCommand(0x12); // DISPLAY_REFRESH
    SendData(0x00);
    ReadBusyH();
}

void Epd::Clear(UBYTE color) {
    UWORD Width = (WIDTH % 4 == 0) ? (WIDTH / 4) : (WIDTH / 4 + 1);
    UWORD Height = HEIGHT;

    SendCommand(0x10);
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            SendData((color << 6) | (color << 4) | (color << 2) | color);
        }
    }
    TurnOnDisplay();
}

void Epd::Display(UBYTE *Image) {
    UWORD Width = (WIDTH % 4 == 0) ? (WIDTH / 4) : (WIDTH / 4 + 1);
    UWORD Height = HEIGHT;

    SendCommand(0x10);
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            SendData(Image[i + j * Width]); // RAM buffer on ESP32
        }
    }
    TurnOnDisplay();
}

void Epd::Display_part(UBYTE *Image, UWORD xstart, UWORD ystart, UWORD image_width, UWORD image_height) {
    UWORD Width = (WIDTH % 4 == 0) ? (WIDTH / 4) : (WIDTH / 4 + 1);
    UWORD Height = HEIGHT;

    SendCommand(0x10);
    for (UWORD i = 0; i < Height; i++) {
        for (UWORD j = 0; j < Width; j++) {
            if (i < image_height + ystart && i >= ystart && j < (image_width + xstart) / 4 && j >= xstart / 4) {
                SendData(Image[(j - xstart / 4) + (image_width / 4 * (i - ystart))]);
            } else {
                SendData(0x55);
            }
        }
    }
    TurnOnDisplay();
}

void Epd::Sleep(void) {
    // POWER_OFF only (safe). Panel will be re-woken by PWR toggle in Init().
    SendCommand(0x02); // POWER_OFF
    SendData(0x00);
    ReadBusyH();
    DelayMs(10);
}
