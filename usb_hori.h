#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HORI_BTN_Y       0x0001
#define HORI_BTN_B       0x0002
#define HORI_BTN_A       0x0004
#define HORI_BTN_X       0x0008
#define HORI_BTN_L       0x0010
#define HORI_BTN_R       0x0020
#define HORI_BTN_ZL      0x0040
#define HORI_BTN_ZR      0x0080
#define HORI_BTN_MINUS   0x0100
#define HORI_BTN_PLUS    0x0200
#define HORI_BTN_LSTICK  0x0400
#define HORI_BTN_RSTICK  0x0800
#define HORI_BTN_HOME    0x1000
#define HORI_BTN_CAPTURE 0x2000

#define HORI_HAT_LEFT   0x06
#define HORI_HAT_CENTER 0x08
#define HORI_STICK_MIN  0x00
#define HORI_STICK_MID  0x80
#define HORI_STICK_MAX  0xFF

typedef struct {
    uint16_t buttons;
    uint8_t hat;
    uint8_t lx;
    uint8_t ly;
    uint8_t rx;
    uint8_t ry;
    uint8_t vendor;
} __attribute__((packed)) HoriReport;

void hori_report_neutral(HoriReport* r);

bool hori_usb_start(void);
void hori_usb_stop(void);
bool hori_usb_is_connected(void);
bool hori_usb_active(void);
uint32_t hori_usb_connection_id(void);

/* The console swallows the first press after connecting */
bool hori_usb_needs_wake(void);
void hori_usb_wake(uint16_t button, uint32_t hold_ms, uint32_t settle_ms);
void hori_usb_set_report(const HoriReport* r);
