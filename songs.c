#include "songs.h"

#include <string.h>

#include "config.h"
#include "ocarina.h"

#define LENGTH(array) (sizeof(array) / sizeof((array)[0]))

#define FRAME_US 16667u

/* Lengths in game frames, from the decomps' sOcarinaSongNotes */
static const OcSong oot_songs[] = {
    {"Zelda's Lullaby", FRAME_US, "<51^25>78<51^25>100"},
    {"Epona's Song", FRAME_US, "^18<18>72^18<18>144"},
    {"Saria's Song", FRAME_US, "v17>17<34v17>17<136"},
    {"Sun's Song", FRAME_US, ">12v13^29.9>12v13^120"},
    {"Song of Time", FRAME_US, ">32A65v33>32A65v99"},
    {"Song of Storms", FRAME_US, "A11v11^45A11v11^90"},
    {"Minuet of Forest", FRAME_US, "A18^18<72>18<18>144"},
    {"Bolero of Fire", FRAME_US, "v15A15v15A15>15v15>15v135"},
    {"Serenade of Water", FRAME_US, "A36v36>33.3>36<144"},
    {"Nocturne of Shadow", FRAME_US, "<36>33.3>18A18<18>18v144"},
    {"Requiem of Spirit", FRAME_US, "A45v23A22>45v45A180"},
    {"Prelude of Light", FRAME_US, "^15>45^15>15<15^60"},
};

static const OcSong mm_songs[] = {
    {"Song of Time", FRAME_US, ">32A65v33>32A65v99"},
    {"Song of Healing", FRAME_US, "<32>33v33<32>33v121"},
    {"Epona's Song", FRAME_US, "^18<18>72^18<18>144"},
    {"Song of Soaring", FRAME_US, "v18<18^36v18<18^108"},
    {"Song of Storms", FRAME_US, "A11v11^45A11v11^90"},
    {"Sonata of Awakening", FRAME_US, "^19<19^19<38A39>76A77"},
    {"Goron Lullaby", FRAME_US, "A41>40<39A42>40<39>41A80"},
    {"New Wave Bossa Nova", FRAME_US, "<64^13<12>78v12<13>114"},
    {"Elegy of Emptiness", FRAME_US, ">85<43>43v21>21^43<85"},
    {"Oath to Order", FRAME_US, ">97v48A49v49>48^97"},
    {"Inverted Song of Time", FRAME_US, "v32A65>33v32A65>99"},
    {"Song of Double Time", FRAME_US, ">29.3>30.3A29.3A30.3v29.3v99"},
};

const SongBook song_books[] = {
    {"Ocarina of Time", oot_songs, LENGTH(oot_songs)},
    {"Majora's Mask", mm_songs, LENGTH(mm_songs)},
};

const size_t song_book_count = LENGTH(song_books);

static bool button_from_letter(char letter, OcButton* out) {
    switch(letter) {
    case 'A':
        *out = OcA;
        return true;
    case 'v':
        *out = OcCDown;
        return true;
    case '>':
        *out = OcCRight;
        return true;
    case '<':
        *out = OcCLeft;
        return true;
    case '^':
        *out = OcCUp;
        return true;
    default:
        return false;
    }
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool songs_build(const OcSong* song, MidiSong* out) {
    memset(out, 0, sizeof(*out));
    const char* s = song->sequence;
    out->notes = malloc(sizeof(MidiNote) * strlen(s));
    if(!out->notes) return false;

    uint32_t t_us = 0;
    while(*s) {
        OcButton button = OcA;
        bool is_rest = *s == '.';
        bool is_note = button_from_letter(*s++, &button);
        if(!is_note && !is_rest) continue;

        uint32_t units = 0;
        while(is_digit(*s))
            units = units * 10 + (uint32_t)(*s++ - '0');
        if(units == 0) units = 1;

        uint32_t length_us = units * song->unit_us;
        if(is_rest) {
            t_us += length_us;
            continue;
        }
        uint32_t length_ms = length_us / 1000;
        uint32_t hold_ms = length_ms > SONG_GAP_MS + MIN_NOTE_MS ? length_ms - SONG_GAP_MS :
                                                                     length_ms;
        out->notes[out->count++] = (MidiNote){
            .start_ms = t_us / 1000,
            .dur_ms = (uint16_t)hold_ms,
            .pitch = oc_button_pitch(button),
        };
        t_us += length_us;
    }
    return true;
}
