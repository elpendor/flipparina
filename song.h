#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    uint32_t start_ms;
    uint16_t dur_ms;
    uint8_t pitch;
    uint8_t vibrato; /* MIDI modulation (CC1), 0-127 */
} MidiNote;

typedef struct {
    MidiNote* notes;
    size_t count;
    uint32_t offset_ms; /* where the first note was in the file; start_ms counts from it */
    bool truncated;
    uint16_t channel_mask;
} MidiSong;

static inline void song_free(MidiSong* song) {
    free(song->notes);
    song->notes = NULL;
    song->count = 0;
}
