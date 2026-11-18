#include "ocarina.h"

#include <math.h>

#include "config.h"

#define SCORE_PER_SKIPPED 100000u
#define SCORE_PER_MOVED   3000u
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

static bool find_combo(int pitch, int8_t bend, OcCombo* out) {
    static const int8_t mod_values[] = {0, 1, -1};
    for(size_t m = 0; m < sizeof(mod_values); m++) {
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

static bool map_exact(int pitch, OcOutput output, OcCombo* out) {
    if(output == OcOutputSpeaker) {
        *out = (OcCombo){.pitch = (uint8_t)pitch};
        return pitch >= SPEAKER_MIN_PITCH && pitch <= SPEAKER_MAX_PITCH;
    }
    bool found = find_combo(pitch, 0, out);
#if USE_STICK_BEND
    found = found || find_combo(pitch, BEND_SEMITONES, out) ||
            find_combo(pitch, -BEND_SEMITONES, out);
#endif
    /* Switch 2 does the ±1 with the D-pad instead of R/Z */
    if(found && output == OcOutputSwitch2) {
        out->pad = out->mod;
        out->mod = 0;
    }
    return found;
}

/* With fold, out-of-range notes move by octaves instead of being skipped */
bool oc_map(int pitch, OcOutput output, bool fold, OcCombo* out) {
    static const int8_t shifts[] = {0, 12, -12, 24, -24, 36, -36};
    size_t tries = fold ? sizeof(shifts) : 1;
    for(size_t i = 0; i < tries; i++) {
        if(map_exact(pitch + shifts[i], output, out)) return true;
    }
    return false;
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
    return HORI_STICK_MID;
}

static const uint16_t switch2_buttons[OcButtonCount] = {
    [OcA] = MAP_SWITCH2_D4,
    [OcCDown] = MAP_SWITCH2_F4,
    [OcCRight] = MAP_SWITCH2_A4,
    [OcCLeft] = MAP_SWITCH2_B4,
    [OcCUp] = MAP_SWITCH2_D5,
};

void oc_combo_report(const OcCombo* combo, OcOutput output, HoriReport* report) {
    hori_report_neutral(report);
    if(output == OcOutputSwitch2) {
        report->buttons |= switch2_buttons[combo->button];
        if(combo->pad > 0) report->hat = PAD_BEND_UP;
        if(combo->pad < 0) report->hat = PAD_BEND_DOWN;
    } else {
        report_n64(combo, report);
    }
    report->ly = bend_stick(combo->bend);
}

bool oc_uses_pad(OcOutput output) {
    return output != OcOutputSpeaker;
}

float oc_pitch_hz(int pitch) {
    return 440.0f * powf(2.0f, (float)(pitch - 69) / 12.0f);
}

OcStats oc_stats(const MidiSong* song, int transpose, OcOutput output, bool fold) {
    OcStats stats = {0};
    for(size_t i = 0; i < song->count; i++) {
        OcCombo combo;
        if(oc_map(song->notes[i].pitch + transpose, output, fold, &combo)) {
            stats.playable++;
            if(combo.bend) stats.bent++;
            if(combo.pitch != song->notes[i].pitch + transpose) stats.moved++;
        } else {
            stats.skipped++;
        }
    }
    return stats;
}

int oc_best_transpose(const MidiSong* song, OcOutput output, bool fold) {
    int best = 0;
    uint32_t best_score = UINT32_MAX;
    for(int t = -TRANSPOSE_SEARCH; t <= TRANSPOSE_SEARCH; t++) {
        OcStats s = oc_stats(song, t, output, fold);
        uint32_t shift = (uint32_t)(t < 0 ? -t : t);
        uint32_t score = (uint32_t)s.skipped * SCORE_PER_SKIPPED +
                         (uint32_t)s.moved * SCORE_PER_MOVED +
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
    OcOutput output,
    bool fold,
    OcCombo* combo) {
    while(from < song->count && !oc_map(song->notes[from].pitch + transpose, output, fold, combo))
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
        /* Release first if the game would hear the same note, or if the stick or D-pad moves */
        if(next_combo->pitch - next_combo->bend - next_combo->pad ==
               combo->pitch - combo->bend - combo->pad ||
           next_combo->bend != combo->bend || next_combo->pad != combo->pad) {
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
