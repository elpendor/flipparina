#pragma once

#include "ocarina.h"
#include "song.h"

typedef void (*PlayerNoteCallback)(int32_t note_index, void* ctx);
typedef void (*PlayerDoneCallback)(void* ctx);

typedef struct Player Player;

Player* player_start(
    const MidiSong* song,
    int transpose,
    int tempo_pct,
    OcLayout layout,
    PlayerNoteCallback on_note,
    PlayerDoneCallback on_done,
    void* ctx);

void player_request_stop(Player* player);

void player_free(Player* player);
