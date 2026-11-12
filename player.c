#include "player.h"

#include <furi.h>
#include <furi_hal_speaker.h>

#include "config.h"
#include "ocarina.h"
#include "usb_hori.h"

#define SPEAKER_ACQUIRE_MS 500

struct Player {
    FuriThread* thread;
    const MidiSong* song;
    int transpose;
    int tempo_pct;
    OcLayout layout;
    volatile bool stop;
    PlayerNoteCallback on_note;
    PlayerDoneCallback on_done;
    void* ctx;
};

static bool wait_until(Player* player, uint32_t target) {
    while((int32_t)(target - furi_get_tick()) > 0) {
        if(player->stop) return false;
        furi_delay_ms(1);
    }
    return !player->stop;
}

static void press(const OcCombo* combo, OcLayout layout, HoriReport* report) {
    if(oc_uses_pad(layout)) {
        oc_combo_report(combo, layout, report);
        hori_usb_set_report(report);
    } else {
        furi_hal_speaker_start(oc_pitch_hz(combo->pitch + 12 * SPEAKER_OCTAVE), SPEAKER_VOLUME);
    }
}

static void release(bool pad, const HoriReport* neutral) {
    if(pad) {
        hori_usb_set_report(neutral);
    } else {
        furi_hal_speaker_stop();
    }
}

static int32_t player_thread(void* ctx) {
    Player* player = ctx;
    const MidiSong* song = player->song;
    OcLayout layout = player->layout;
    bool pad = oc_uses_pad(layout);
    HoriReport neutral, report;
    hori_report_neutral(&neutral);
    hori_usb_set_report(&neutral);

    if(pad) {
        if(hori_usb_needs_wake())
            hori_usb_wake(oc_wake_button(layout), WAKE_HOLD_MS, WAKE_SETTLE_MS);
    } else if(!furi_hal_speaker_acquire(SPEAKER_ACQUIRE_MS)) {
        player->on_done(player->ctx);
        return 0;
    }

    OcCombo combo, next_combo = {0};
    size_t i = oc_next_playable(song, 0, player->transpose, layout, &combo);
    uint32_t t0 = furi_get_tick();

    while(i < song->count) {
        size_t next = oc_next_playable(song, i + 1, player->transpose, layout, &next_combo);
        OcNoteWindow window = oc_note_window(
            &song->notes[i],
            &combo,
            next < song->count ? &song->notes[next] : NULL,
            &next_combo,
            layout,
            player->tempo_pct);

        if(!wait_until(player, t0 + window.press_ms)) break;
        press(&combo, layout, &report);
        player->on_note((int32_t)i, player->ctx);

        if(window.release) {
            if(!wait_until(player, t0 + window.release_ms)) break;
            release(pad, &neutral);
            player->on_note(-1, player->ctx);
        }

        i = next;
        combo = next_combo;
    }

    release(pad, &neutral);
    if(!pad) furi_hal_speaker_release();
    player->on_done(player->ctx);
    return 0;
}

Player* player_start(
    const MidiSong* song,
    int transpose,
    int tempo_pct,
    OcLayout layout,
    PlayerNoteCallback on_note,
    PlayerDoneCallback on_done,
    void* ctx) {
    Player* player = malloc(sizeof(Player));
    *player = (Player){
        .song = song,
        .transpose = transpose,
        .tempo_pct = tempo_pct,
        .layout = layout,
        .on_note = on_note,
        .on_done = on_done,
        .ctx = ctx,
    };
    player->thread =
        furi_thread_alloc_ex("FlipparinaPlayer", PLAYER_STACK_SIZE, player_thread, player);
    furi_thread_set_priority(player->thread, FuriThreadPriorityHigh);
    furi_thread_start(player->thread);
    return player;
}

void player_request_stop(Player* player) {
    player->stop = true;
}

void player_free(Player* player) {
    player->stop = true;
    furi_thread_join(player->thread);
    furi_thread_free(player->thread);
    free(player);
}
