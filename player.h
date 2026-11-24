#pragma once

#include "ocarina.h"
#include "song.h"
#include "usb_hori.h"

typedef void (*PlayerNoteCallback)(int32_t note_index, void* ctx);
typedef void (*PlayerDoneCallback)(void* ctx);

typedef struct Player Player;
/* harmony (pad outputs only): a second part played on the Flipper speaker, NULL for none.
   harmony_delay_ms holds it back to line up with the game's ocarina. */
Player* player_start(
    const MidiSong* song,
    int transpose,
    int tempo_pct,
    OcOutput output,
    bool fold,
    const HoriReport* take_out,
    const MidiSong* harmony,
    uint32_t harmony_delay_ms,
    PlayerNoteCallback on_note,
    PlayerDoneCallback on_done,
    void* ctx);

bool player_take_control(void);

void player_request_stop(Player* player);

void player_free(Player* player);
