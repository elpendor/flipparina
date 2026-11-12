#pragma once

#include "song.h"
#include "usb_hori.h"

typedef enum {
    OcA,
    OcCDown,
    OcCRight,
    OcCLeft,
    OcCUp,
    OcButtonCount,
} OcButton;

typedef enum {
    OcLayoutN64,
    OcLayoutRemake,
    OcLayoutSpeaker,
    OcLayoutCount,
} OcLayout;

typedef struct {
    uint8_t pitch;
    OcButton button;
    int8_t mod;
    int8_t bend;
} OcCombo;

typedef struct {
    size_t playable;
    size_t bent;
    size_t skipped;
} OcStats;

typedef struct {
    uint32_t press_ms;
    uint32_t release_ms;
    bool release;
} OcNoteWindow;

uint8_t oc_button_pitch(OcButton button);
const char* oc_pitch_class_name(int pitch);

bool oc_map(int pitch, OcLayout layout, OcCombo* out);

uint16_t oc_wake_button(OcLayout layout);

bool oc_uses_pad(OcLayout layout);

float oc_pitch_hz(int pitch);

void oc_combo_report(const OcCombo* combo, OcLayout layout, HoriReport* report);

OcStats oc_stats(const MidiSong* song, int transpose, OcLayout layout);

int oc_best_transpose(const MidiSong* song, OcLayout layout);

size_t oc_next_playable(
    const MidiSong* song,
    size_t from,
    int transpose,
    OcLayout layout,
    OcCombo* combo);

OcNoteWindow oc_note_window(
    const MidiNote* note,
    const OcCombo* combo,
    const MidiNote* next,
    const OcCombo* next_combo,
    OcLayout layout,
    int tempo_pct);
