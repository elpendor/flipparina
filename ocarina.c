#include "ocarina.h"

#include <math.h>

#include "config.h"

#define SCORE_PER_SKIPPED 100000u
#define SCORE_PER_BEND    1000u
#define SCORE_NOT_OCTAVE  100u

static const uint8_t base_pitch[OcButtonCount] = {62, 65, 69, 71, 74};
static const char* const pitch_class_names[12] =
    {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

uint8_t oc_button_pitch(OcButton button) {
    return base_pitch[button];
}

const char* oc_pitch_class_name(int pitch) {
    return pitch_class_names[((pitch % 12) + 12) % 12];
}

static bool find_combo(int pitch, int8_t bend, bool mods, OcCombo* out) {
    static const int8_t mod_values[] = {0, 1, -1};
    size_t mod_count = mods ? sizeof(mod_values) : 1;
    for(size_t m = 0; m < mod_count; m++) {
        for(int b = 0; b < OcButtonCount; b++) {
            if(base_pitch[b] + mod_values[m] + bend == pitch) {
                *out = (OcCombo){
                    .pitch = (uint8_t)pitch,
                    .button = (OcButton)b,
                    .mod = mod_values[m],
                    .bend = bend,
                };
                return true;
            }
        }
    }
    return false;
}

bool oc_map(int pitch, OcLayout layout, OcCombo* out) {
    if(layout == OcLayoutRemake) {
        static const int8_t bends[] = {0, BEND_SEMITONES, -BEND_SEMITONES, 1, -1};
        for(size_t i = 0; i < sizeof(bends); i++) {
            if(find_combo(pitch, bends[i], false, out)) return true;
        }
        return false;
    }
    if(layout == OcLayoutSpeaker) {
        *out = (OcCombo){.pitch = (uint8_t)pitch};
        return pitch >= SPEAKER_MIN_PITCH && pitch <= SPEAKER_MAX_PITCH;
    }
    if(find_combo(pitch, 0, true, out)) return true;
#if USE_STICK_BEND
    return find_combo(pitch, BEND_SEMITONES, true, out) ||
           find_combo(pitch, -BEND_SEMITONES, true, out);
#else
    return false;
#endif
}

static void report_n64(const OcCombo* combo, HoriReport* report) {
    switch(combo->button) {
    case OcA:
        report->buttons |= MAP_N64_A;
        break;
    case OcCUp:
        report->ry = HORI_STICK_MIN;
        break;
    case OcCDown:
        report->ry = HORI_STICK_MAX;
        break;
    case OcCLeft:
        report->rx = HORI_STICK_MIN;
        break;
    case OcCRight:
        report->rx = HORI_STICK_MAX;
        break;
    default:
        break;
    }
    if(combo->mod < 0) report->buttons |= MAP_N64_Z;
    if(combo->mod > 0) report->buttons |= MAP_N64_R;
}

static uint8_t bend_stick(int8_t bend) {
    if(bend >= BEND_SEMITONES) return BEND_STICK_UP;
    if(bend <= -BEND_SEMITONES) return BEND_STICK_DOWN;
    if(bend > 0) return BEND_HALF_UP;
    if(bend < 0) return BEND_HALF_DOWN;
    return HORI_STICK_MID;
}

static const uint16_t remake_buttons[OcButtonCount] = {
    [OcA] = MAP_REMAKE_D4,
    [OcCDown] = MAP_REMAKE_F4,
    [OcCRight] = MAP_REMAKE_A4,
    [OcCLeft] = MAP_REMAKE_B4,
    [OcCUp] = MAP_REMAKE_D5,
};

void oc_combo_report(const OcCombo* combo, OcLayout layout, HoriReport* report) {
    hori_report_neutral(report);
    if(layout == OcLayoutRemake) {
        report->buttons |= remake_buttons[combo->button];
    } else {
        report_n64(combo, report);
    }
    report->ly = bend_stick(combo->bend);
}

bool oc_uses_pad(OcLayout layout) {
    return layout != OcLayoutSpeaker;
}

float oc_pitch_hz(int pitch) {
    return 440.0f * powf(2.0f, (float)(pitch - 69) / 12.0f);
}

OcStats oc_stats(const MidiSong* song, int transpose, OcLayout layout) {
    OcStats stats = {0};
    for(size_t i = 0; i < song->count; i++) {
        OcCombo combo;
        if(oc_map(song->notes[i].pitch + transpose, layout, &combo)) {
            stats.playable++;
            if(combo.bend) stats.bent++;
        } else {
            stats.skipped++;
        }
    }
    return stats;
}

int oc_best_transpose(const MidiSong* song, OcLayout layout) {
    int best = 0;
    uint32_t best_score = UINT32_MAX;
    for(int t = -TRANSPOSE_SEARCH; t <= TRANSPOSE_SEARCH; t++) {
        OcStats s = oc_stats(song, t, layout);
        uint32_t shift = (uint32_t)(t < 0 ? -t : t);
        uint32_t score = (uint32_t)s.skipped * SCORE_PER_SKIPPED +
                         (uint32_t)s.bent * SCORE_PER_BEND +
                         ((shift % 12) ? SCORE_NOT_OCTAVE : 0u) + shift;
        if(score < best_score) {
            best_score = score;
            best = t;
        }
    }
    return best;
}

size_t oc_next_playable(
    const MidiSong* song,
    size_t from,
    int transpose,
    OcLayout layout,
    OcCombo* combo) {
    while(from < song->count && !oc_map(song->notes[from].pitch + transpose, layout, combo))
        from++;
    return from;
}

static uint32_t scale_ms(uint32_t ms, int tempo_pct) {
    return (uint32_t)((uint64_t)ms * 100 / (uint32_t)tempo_pct);
}

OcNoteWindow oc_note_window(
    const MidiNote* note,
    const OcCombo* combo,
    const MidiNote* next,
    const OcCombo* next_combo,
    int tempo_pct) {
    OcNoteWindow w = {.release = true};
    w.press_ms = scale_ms(note->start_ms, tempo_pct);

    uint32_t end = scale_ms(note->start_ms + note->dur_ms, tempo_pct);
    if(end < w.press_ms + MIN_NOTE_MS) end = w.press_ms + MIN_NOTE_MS;

    if(next) {
        uint32_t next_start = scale_ms(next->start_ms, tempo_pct);
        uint32_t limit = next_start;
        /* The game only starts a new note when the unbent pitch changes, and the
           remake misses notes whose button and bend change at the same time */
        if(next_combo->pitch - next_combo->bend == combo->pitch - combo->bend ||
           next_combo->bend != combo->bend) {
            limit = (next_start > w.press_ms + RETRIGGER_MS) ?
                        next_start - RETRIGGER_MS :
                        w.press_ms + (next_start - w.press_ms) / 2;
        }
        if(end > limit) end = limit;
        if(end <= w.press_ms) end = w.press_ms + 1;
        w.release = end < next_start;
    }
    w.release_ms = end;
    return w;
}
