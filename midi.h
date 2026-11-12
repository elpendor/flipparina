#pragma once

#include <furi.h>
#include <storage/storage.h>

#include "song.h"

/* Reduces a MIDI file to one melody line; channel -1 = all */
bool midi_load_file(
    Storage* storage,
    const char* path,
    int channel,
    MidiSong* song,
    FuriString* error);
