#pragma once

#include "usb_hori.h"

/* NSO N64 defaults; C-buttons go on the right stick */
#define MAP_N64_A HORI_BTN_A
#define MAP_N64_Z HORI_BTN_ZL
#define MAP_N64_R HORI_BTN_R

/* Switch 2: from community guides, untested */
#define MAP_REMAKE_D4 HORI_BTN_L
#define MAP_REMAKE_F4 HORI_BTN_R
#define MAP_REMAKE_A4 HORI_BTN_A
#define MAP_REMAKE_B4 HORI_BTN_Y
#define MAP_REMAKE_D5 HORI_BTN_X

#define SPEAKER_VOLUME    1.0f
#define SPEAKER_OCTAVE    0
#define SPEAKER_MIN_PITCH 36
#define SPEAKER_MAX_PITCH 96

#define BEND_STICK_UP   0x00
#define BEND_STICK_DOWN 0xFF
#define USE_STICK_BEND  1

/* OoT runs at 20 fps */
#define MIN_NOTE_MS  70
#define RETRIGGER_MS 70

#define SONG_GAP_MS 60

/* The console swallows the first press after connecting */
#define WAKE_BUTTON_N64    HORI_BTN_L
#define WAKE_BUTTON_REMAKE HORI_BTN_RSTICK
#define WAKE_HOLD_MS   100
#define WAKE_SETTLE_MS 400

/* Change Grip/Order ignores short presses */
#define TAP_HOLD_MS 500

#define TEMPO_MIN_PCT    30
#define TEMPO_MAX_PCT    200
#define TEMPO_STEP_PCT   10
#define TRANSPOSE_LIMIT  48
#define TRANSPOSE_SEARCH 24

#define PLAYER_STACK_SIZE 2048
