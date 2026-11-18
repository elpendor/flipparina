#pragma once

#include "usb_hori.h"

/* NSO N64 defaults; C-buttons go on the right stick */
#define MAP_N64_A HORI_BTN_A
#define MAP_N64_Z HORI_BTN_ZL
#define MAP_N64_R HORI_BTN_R

/* Switch 2 note buttons */
#define MAP_SWITCH2_D4 HORI_BTN_L
#define MAP_SWITCH2_F4 HORI_BTN_R
#define MAP_SWITCH2_A4 HORI_BTN_A
#define MAP_SWITCH2_B4 HORI_BTN_Y
#define MAP_SWITCH2_D5 HORI_BTN_X

#define SPEAKER_VOLUME    1.0f
#define SPEAKER_FULL_DUTY 1
#define SPEAKER_OCTAVE    0
#define SPEAKER_MIN_PITCH 36
#define SPEAKER_MAX_PITCH 96

/* Full tilt is an exact 2-semitone bend: the game clamps there */
#define BEND_STICK_UP   0x00
#define BEND_STICK_DOWN 0xFF
#define BEND_SEMITONES  2
#define USE_STICK_BEND  1

/* Vibrato: stick tilt right at full MIDI modulation (CC1 = 127) */
#define VIBRATO_MAX_TILT 0x7F

/* Switch 2: D-pad up/down bends 1 semitone, like R/Z on N64 */
#define PAD_BEND_UP   HORI_HAT_UP
#define PAD_BEND_DOWN HORI_HAT_DOWN

/* OoT runs at 20 fps */
#define MIN_NOTE_MS  70
#define RETRIGGER_MS 70

#define SONG_GAP_MS 60

/* The console swallows the first press after connecting */
#define WAKE_BUTTON    HORI_BTN_L
#define WAKE_HOLD_MS   100
#define WAKE_SETTLE_MS 400

/* Take control: R-stick click opens the Controllers screen, L+R registers, A confirms */
#define APPLET_OPEN_BUTTON HORI_BTN_RSTICK
#define APPLET_HOLD_MS  200
#define APPLET_OPEN_MS  800
#define APPLET_STEP_MS  500
#define APPLET_CLOSE_MS 800
#define REPLUG_MS       500
#define REPLUG_TIMEOUT_MS 3000
#define OCARINA_OPEN_MS 1000

#define TEMPO_MIN_PCT    30
#define TEMPO_MAX_PCT    200
#define TEMPO_STEP_PCT   10
#define TRANSPOSE_LIMIT  48
#define TRANSPOSE_SEARCH 24

#define PLAYER_STACK_SIZE 2048

/* 1: fake the USB pad so qFlipper stays connected for screenshots */
#define SCREENSHOT_MODE 0
