#include "player.h"

#include <furi.h>
#include <furi_hal_speaker.h>
#include <stm32wbxx_ll_tim.h>

#include "config.h"
#include "ocarina.h"
#include "usb_hori.h"

#define SPEAKER_ACQUIRE_MS 500

struct Player {
    FuriThread* thread;
    const MidiSong* song;
    int transpose;
    int tempo_pct;
    OcOutput output;
    bool fold;
    bool take_out;
    HoriReport take_out_report;
    const MidiSong* harmony;
    uint32_t harmony_delay_ms;
    uint32_t harmony_t0;
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

static void take_out_ocarina(const HoriReport* press) {
    HoriReport neutral;
    hori_report_neutral(&neutral);
    hori_usb_set_report(press);
    furi_delay_ms(WAKE_HOLD_MS);
    hori_usb_set_report(&neutral);
    furi_delay_ms(OCARINA_OPEN_MS);
}

/* Replugging always brings up the Controllers screen */
bool player_take_control(void) {
    hori_usb_stop();
    furi_delay_ms(REPLUG_MS);
    if(!hori_usb_start()) return false;
    for(uint32_t t = 0; !hori_usb_is_connected(); t += 50) {
        if(t >= REPLUG_TIMEOUT_MS) return false;
        furi_delay_ms(50);
    }
    hori_usb_wake(APPLET_OPEN_BUTTON, WAKE_HOLD_MS, APPLET_OPEN_MS);
    hori_usb_wake(HORI_BTN_L | HORI_BTN_R, APPLET_HOLD_MS, APPLET_STEP_MS);
    hori_usb_wake(HORI_BTN_A, APPLET_HOLD_MS, APPLET_CLOSE_MS);
    return true;
}

static void speaker_play(int pitch) {
    furi_hal_speaker_start(oc_pitch_hz(pitch + 12 * SPEAKER_OCTAVE), SPEAKER_VOLUME);
#if SPEAKER_FULL_DUTY
    LL_TIM_OC_SetCompareCH1(TIM16, LL_TIM_GetAutoReload(TIM16) / 2);
#endif
}

/* Folds by octaves into the speaker's range */
static int speaker_pitch(int pitch) {
    while(pitch < SPEAKER_MIN_PITCH)
        pitch += 12;
    while(pitch > SPEAKER_MAX_PITCH)
        pitch -= 12;
    return pitch;
}

/* The harmony keeps the file's own timing: the pad's note windows only suit the game */
static int32_t harmony_thread(void* ctx) {
    Player* player = ctx;
    const MidiSong* h = player->harmony;
    if(!furi_hal_speaker_acquire(SPEAKER_ACQUIRE_MS)) return 0;

    for(size_t i = 0; i < h->count; i++) {
        const MidiNote* note = &h->notes[i];
        uint32_t start = oc_scale_ms(note->start_ms, player->tempo_pct);
        uint32_t end = oc_scale_ms(note->start_ms + note->dur_ms, player->tempo_pct);
        if(!wait_until(player, player->harmony_t0 + start)) break;
        speaker_play(speaker_pitch(note->pitch + player->transpose));
        if(!wait_until(player, player->harmony_t0 + end)) break;
        /* Legato notes change pitch without a gap */
        bool legato = i + 1 < h->count &&
                      oc_scale_ms(h->notes[i + 1].start_ms, player->tempo_pct) <= end;
        if(!legato) furi_hal_speaker_stop();
    }

    furi_hal_speaker_stop();
    furi_hal_speaker_release();
    return 0;
}

static void press(const OcCombo* combo, uint8_t vibrato, OcOutput output, HoriReport* report) {
    if(oc_uses_pad(output)) {
        oc_combo_report(combo, output, report);
        /* A sideways stick tilt adds vibrato; how far sets the depth */
        report->lx = HORI_STICK_MID + vibrato * VIBRATO_MAX_TILT / 127;
        hori_usb_set_report(report);
    } else {
        speaker_play(combo->pitch);
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
    OcOutput output = player->output;
    bool pad = oc_uses_pad(output);
    HoriReport neutral, report;
    hori_report_neutral(&neutral);
    hori_usb_set_report(&neutral);

    if(pad) {
        if(output == OcOutputN64 && hori_usb_needs_wake())
            hori_usb_wake(WAKE_BUTTON, WAKE_HOLD_MS, WAKE_SETTLE_MS);
        if(player->take_out) take_out_ocarina(&player->take_out_report);
    } else if(!furi_hal_speaker_acquire(SPEAKER_ACQUIRE_MS)) {
        player->on_done(player->ctx);
        return 0;
    }

    OcCombo combo, next_combo = {0};
    size_t i = oc_next_playable(song, 0, player->transpose, output, player->fold, &combo);
    uint32_t t0 = furi_get_tick();

    /* Both parts keep their places in the file, whichever starts first */
    FuriThread* harmony = NULL;
    if(player->harmony) {
        uint32_t m_off = song->offset_ms, h_off = player->harmony->offset_ms;
        uint32_t first = m_off < h_off ? m_off : h_off;
        player->harmony_t0 =
            t0 + oc_scale_ms(h_off - first, player->tempo_pct) + player->harmony_delay_ms;
        t0 += oc_scale_ms(m_off - first, player->tempo_pct);
        harmony = furi_thread_alloc_ex(
            "FlipparinaHarmony", PLAYER_STACK_SIZE, harmony_thread, player);
        furi_thread_set_priority(harmony, FuriThreadPriorityHigh);
        furi_thread_start(harmony);
    }

    while(i < song->count) {
        size_t next = oc_next_playable(song, i + 1, player->transpose, output, player->fold, &next_combo);
        OcNoteWindow window = oc_note_window(
            &song->notes[i],
            &combo,
            next < song->count ? &song->notes[next] : NULL,
            &next_combo,
            player->tempo_pct);

        if(!wait_until(player, t0 + window.press_ms)) break;
        press(&combo, song->notes[i].vibrato, output, &report);
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
    player->on_note(-1, player->ctx);
    /* Let the harmony finish its last notes unless stopped */
    if(harmony) {
        furi_thread_join(harmony);
        furi_thread_free(harmony);
    }
    player->on_done(player->ctx);
    return 0;
}

static const MidiSong* pad_harmony(OcOutput output, const MidiSong* harmony) {
    if(!oc_uses_pad(output) || !harmony || harmony->count == 0) return NULL;
    return harmony;
}

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
    void* ctx) {
    Player* player = malloc(sizeof(Player));
    *player = (Player){
        .song = song,
        .transpose = transpose,
        .tempo_pct = tempo_pct,
        .output = output,
        .fold = fold,
        .take_out = take_out != NULL,
        .harmony = pad_harmony(output, harmony),
        .harmony_delay_ms = harmony_delay_ms,
        .on_note = on_note,
        .on_done = on_done,
        .ctx = ctx,
    };
    player->thread =
        furi_thread_alloc_ex("FlipparinaPlayer", PLAYER_STACK_SIZE, player_thread, player);
    if(take_out) player->take_out_report = *take_out;
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
