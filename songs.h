#pragma once

#include "song.h"

typedef struct {
    const char* name;
    uint32_t unit_us;
    /* A v > < ^ = A, C-down, C-right, C-left, C-up; . = rest; optional length in units */
    const char* sequence;
} OcSong;

typedef struct {
    const char* title;
    const OcSong* songs;
    size_t count;
} SongBook;

extern const SongBook song_books[];
extern const size_t song_book_count;

bool songs_build(const OcSong* song, MidiSong* out);
