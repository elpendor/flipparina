#include <furi.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "flipparina_icons.h"
#include "midi.h"
#include "ocarina.h"
#include "player.h"
#include "songs.h"
#include "usb_hori.h"

#define DATA_DIR    EXT_PATH("apps_data/flipparina")
#define OUTPUT_FILE DATA_DIR "/layout.txt"
#define OCARINA_C_FILE DATA_DIR "/ocarina_c.txt"
#define FOLD_FILE      DATA_DIR "/out_of_range.txt"

#define SCREEN_W      128
#define HEADER_Y      10
#define LINE_Y(n)     (12 + 10 * (n))
#define LIST_ROWS     5
#define STATUS_GAP    4
#define NOTICE_MS     1500
#define QUEUE_SIZE    8
#define IDLE_POLL_MS  250
#define REDRAW_MS     500

typedef enum {
    ScreenMenu,
    ScreenSettings,
    ScreenSongs,
    ScreenPlayer,
} Screen;

typedef enum {
    MenuOotSongs,
    MenuMmSongs,
    MenuMidiFile,
    MenuTakeControl,
    MenuSettings,
    MenuCount,
} MenuItem;

static const char* const menu_labels[MenuCount] = {
    "Ocarina of Time songs",
    "Majora's Mask songs",
    "MIDI files",
    "Take control",
    "Settings",
};

typedef enum {
    SettingOutput,
    SettingOcarinaC,
    SettingFold,
    SettingCount,
} SettingItem;

static const char* const fold_ids[] = {"octave", "skip"};

static const char* const output_labels[OcOutputCount] = {
    [OcOutputN64] = "Output: N64 (NSO)",
    [OcOutputSwitch2] = "Output: Switch 2",
    [OcOutputSpeaker] = "Output: Speaker (mic)",
};

static const char* const output_ids[OcOutputCount] = {
    [OcOutputN64] = "n64",
    [OcOutputSwitch2] = "remake",
    [OcOutputSpeaker] = "speaker",
};

/* N64: the C button the ocarina is assigned to */
static const OcButton ocarina_c_buttons[] = {OcCLeft, OcCDown, OcCRight};
static const char* const ocarina_c_labels[] = {
    "Ocarina on: C-Left",
    "Ocarina on: C-Down",
    "Ocarina on: C-Right",
};
static const char* const ocarina_c_ids[] = {"left", "down", "right"};
#define OCARINA_C_COUNT 3

typedef enum {
    ActionNone,
    ActionExit,
    ActionPickFile,
    ActionTakeControl,
    ActionAskOcarina,
    ActionFirstPlay,
    ActionOutputChanged,
} Action;

typedef enum {
    EvInput,
    EvPlayerDone,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} AppEvent;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* queue;
    FuriMutex* mutex;
    DialogsApp* dialogs;
    bool usb_ok;
    uint32_t controlled_id; /* connection the Flipper took control on */
    OcOutput output;
    int ocarina_c; /* index into ocarina_c_buttons, -1 = ask */
    bool fold;

    Screen screen;
    Screen player_back;
    int menu_index;
    int settings_index;
    int book;
    int song_index;

    FuriString* path;
    FuriString* title;
    FuriString* error;
    MidiSong song;
    bool builtin;
    int channel;
    int transpose;
    int tempo_pct;
    OcStats stats;

    Player* player;
    int32_t current_note;

    const char* notice;
    uint32_t notice_until;
} App;

static bool is_playing(const App* app) {
    return app->player != NULL;
}

static void reset_song(App* app, const char* title) {
    song_free(&app->song);
    furi_string_reset(app->error);
    furi_string_set(app->title, title);
    app->transpose = 0;
    app->tempo_pct = 100;
}

static bool load_file(App* app, int channel) {
    const char* path = furi_string_get_cstr(app->path);
    const char* slash = strrchr(path, '/');
    reset_song(app, slash ? slash + 1 : path);
    app->builtin = false;
    app->channel = channel;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = midi_load_file(storage, path, channel, &app->song, app->error);
    furi_record_close(RECORD_STORAGE);

    if(ok) app->transpose = oc_best_transpose(&app->song, app->output, app->fold);
    app->stats = oc_stats(&app->song, app->transpose, app->output, app->fold);
    return ok;
}

static void load_builtin(App* app, const OcSong* song) {
    reset_song(app, song->name);
    app->builtin = true;
    app->channel = -1;
    if(!songs_build(song, &app->song)) furi_string_set(app->error, "Out of memory");
    app->stats = oc_stats(&app->song, app->transpose, app->output, app->fold);
}

static int cycle_channel(const App* app, int dir) {
    int ch = app->channel;
    for(int tries = 0; tries <= 16; tries++) {
        ch += dir;
        if(ch > 15) ch = -1;
        if(ch < -1) ch = 15;
        if(ch == -1 || (app->song.channel_mask & (1u << ch))) return ch;
    }
    return -1;
}

static void change_channel(App* app, int dir) {
    uint16_t mask = app->song.channel_mask;
    int tempo = app->tempo_pct;
    if(!load_file(app, cycle_channel(app, dir))) app->song.channel_mask = mask;
    app->tempo_pct = tempo;
}

static bool is_midi_name(const char* name) {
    size_t len = strlen(name);
    return len > 4 && strcasecmp(name + len - 4, ".mid") == 0;
}

/* Next/previous .mid file in the current file's folder, alphabetically, wrapping */
static void change_file(App* app, int dir) {
    FuriString* folder = furi_string_alloc_set(app->path);
    size_t slash = furi_string_search_rchar(folder, '/');
    if(slash == FURI_STRING_FAILURE) {
        furi_string_free(folder);
        return;
    }
    const char* current = furi_string_get_cstr(app->path) + slash + 1;
    furi_string_left(folder, slash);

    char name[128];
    FuriString* best = furi_string_alloc();
    FuriString* wrap = furi_string_alloc();
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* d = storage_file_alloc(storage);
    if(storage_dir_open(d, furi_string_get_cstr(folder))) {
        FileInfo info;
        while(storage_dir_read(d, &info, name, sizeof(name))) {
            if((info.flags & FSF_DIRECTORY) || !is_midi_name(name)) continue;
            int order = strcmp(name, current) * dir;
            const char* b = furi_string_get_cstr(best);
            const char* w = furi_string_get_cstr(wrap);
            if(order > 0 && (furi_string_empty(best) || strcmp(name, b) * dir < 0))
                furi_string_set(best, name);
            if(furi_string_empty(wrap) || strcmp(name, w) * dir < 0) furi_string_set(wrap, name);
        }
    }
    storage_dir_close(d);
    storage_file_free(d);
    furi_record_close(RECORD_STORAGE);

    FuriString* pick = furi_string_empty(best) ? wrap : best;
    if(!furi_string_empty(pick) && strcmp(furi_string_get_cstr(pick), current) != 0) {
        int tempo = app->tempo_pct;
        furi_string_printf(
            app->path, "%s/%s", furi_string_get_cstr(folder), furi_string_get_cstr(pick));
        load_file(app, -1);
        app->tempo_pct = tempo;
    }
    furi_string_free(best);
    furi_string_free(wrap);
    furi_string_free(folder);
}

static void change_transpose(App* app, int delta) {
    int t = app->transpose + delta;
    if(t < -TRANSPOSE_LIMIT || t > TRANSPOSE_LIMIT) return;
    app->transpose = t;
    app->stats = oc_stats(&app->song, t, app->output, app->fold);
}

static void change_tempo(App* app, int delta) {
    int t = app->tempo_pct + delta;
    if(t >= TEMPO_MIN_PCT && t <= TEMPO_MAX_PCT) app->tempo_pct = t;
}

static int load_setting(
    Storage* storage,
    const char* path,
    const char* const* ids,
    int count,
    int fallback) {
    int value = fallback;
    char id[16] = {0};
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_read(file, id, sizeof(id) - 1);
        for(int i = 0; i < count; i++) {
            if(strcmp(id, ids[i]) == 0) value = i;
        }
    }
    storage_file_free(file);
    return value;
}

static void save_setting(const char* path, const char* id) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, id, strlen(id));
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

/* The speaker output must not show up as a controller */
static void update_usb(App* app) {
    bool want = oc_uses_pad(app->output);
    if(want == app->usb_ok) return;
    bool ok = want && hori_usb_start();
    if(!want) hori_usb_stop();
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->usb_ok = ok;
    furi_mutex_release(app->mutex);
}

static void on_player_note(int32_t note_index, void* ctx) {
    App* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->current_note = note_index;
    furi_mutex_release(app->mutex);
    view_port_update(app->view_port);
}

static void on_player_done(void* ctx) {
    App* app = ctx;
    AppEvent ev = {.type = EvPlayerDone};
    furi_message_queue_put(app->queue, &ev, FuriWaitForever);
}

static bool can_play(const App* app) {
    if(is_playing(app) || !furi_string_empty(app->error)) return false;
    if(oc_uses_pad(app->output) && !app->usb_ok) return false;
    return app->stats.playable > 0;
}

static void start_playback(App* app, bool take_out) {
    if(!can_play(app)) return;
    app->current_note = -1;
    HoriReport report;
    hori_report_neutral(&report);
    if(app->output == OcOutputSwitch2) {
        report.hat = HORI_HAT_LEFT;
    } else if(take_out && app->ocarina_c >= 0) {
        OcCombo item = {.button = ocarina_c_buttons[app->ocarina_c]};
        oc_combo_report(&item, OcOutputN64, &report);
    }
    app->player = player_start(
        &app->song,
        app->transpose,
        app->tempo_pct,
        app->output,
        app->fold,
        take_out ? &report : NULL,
        on_player_note,
        on_player_done,
        app);
}

static void show_notice_for(App* app, const char* text, uint32_t ms) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->notice = text;
    app->notice_until = furi_get_tick() + ms;
    furi_mutex_release(app->mutex);
    view_port_update(app->view_port);
}

static void show_notice(App* app, const char* text) {
    show_notice_for(app, text, NOTICE_MS);
}

typedef const char* (*ListLabel)(size_t index, const void* ctx);

static int visible_menu(const App* app, MenuItem* items) {
    int count = 0;
    for(int i = 0; i < MenuCount; i++) {
        if(i == MenuTakeControl && !oc_uses_pad(app->output)) continue;
        items[count++] = (MenuItem)i;
    }
    return count;
}

static const char* menu_label(size_t position, const void* ctx) {
    MenuItem items[MenuCount];
    visible_menu(ctx, items);
    return menu_labels[items[position]];
}

static int visible_settings(const App* app, SettingItem* items) {
    int count = 0;
    for(int i = 0; i < SettingCount; i++) {
        if(i == SettingOcarinaC && app->output != OcOutputN64) continue;
        items[count++] = (SettingItem)i;
    }
    return count;
}

static const char* setting_label(size_t position, const void* ctx) {
    const App* app = ctx;
    SettingItem items[SettingCount];
    visible_settings(app, items);
    switch(items[position]) {
    case SettingOutput:
        return output_labels[app->output];
    case SettingOcarinaC:
        return app->ocarina_c < 0 ? "Ocarina on: ask" : ocarina_c_labels[app->ocarina_c];
    case SettingFold:
        return app->fold ? "Out of range: Octave" : "Out of range: Skip";
    default:
        return "";
    }
}

static const char* song_label(size_t index, const void* ctx) {
    const SongBook* book = ctx;
    return book->songs[index].name;
}

/* Drawn as icons: the fonts have no arrows */
#define ARROW_UP    "\x01"
#define ARROW_DOWN  "\x02"
#define ARROW_LEFT  "\x03"
#define ARROW_RIGHT "\x04"
#define ICON_GAP    1

static const char* const button_labels[OcOutputCount][OcButtonCount] = {
    [OcOutputN64] =
        {
            [OcA] = "A",
            [OcCDown] = "C" ARROW_DOWN,
            [OcCRight] = "C" ARROW_RIGHT,
            [OcCLeft] = "C" ARROW_LEFT,
            [OcCUp] = "C" ARROW_UP,
        },
    [OcOutputSwitch2] =
        {
            [OcA] = "L",
            [OcCDown] = "R",
            [OcCRight] = "A",
            [OcCLeft] = "Y",
            [OcCUp] = "X",
        },
};

static void combo_label(const App* app, const OcCombo* combo, char* out, size_t size) {
    if(!oc_uses_pad(app->output)) {
        snprintf(out, size, "%s%d", oc_pitch_class_name(combo->pitch), combo->pitch / 12 - 1);
        return;
    }
    snprintf(
        out,
        size,
        "%s%s%s%s",
        button_labels[app->output][combo->button],
        combo->mod > 0 ? "+R" : (combo->mod < 0 ? "+Z" : ""),
        combo->pad > 0 ? "+pad" ARROW_UP : (combo->pad < 0 ? "+pad" ARROW_DOWN : ""),
        combo->bend > 0 ? "+bend" ARROW_UP : (combo->bend < 0 ? "+bend" ARROW_DOWN : ""));
}

static const Icon* arrow_icon(char code) {
    switch(code) {
    case '\x01':
        return &I_arrow_up_7x4;
    case '\x02':
        return &I_arrow_down_7x4;
    case '\x03':
        return &I_arrow_left_4x7;
    case '\x04':
        return &I_arrow_right_4x7;
    default:
        return NULL;
    }
}

static void draw_text(Canvas* canvas, int x, int y, const char* text) {
    char run[64];
    size_t len = 0;
    for(const char* p = text;; p++) {
        const Icon* icon = arrow_icon(*p);
        if(*p == '\0' || icon || len == sizeof(run) - 1) {
            run[len] = '\0';
            if(len) {
                canvas_draw_str(canvas, x, y, run);
                x += canvas_string_width(canvas, run);
                len = 0;
            }
            if(*p == '\0') return;
            if(icon) {
                int top = y - 4 - icon_get_height(icon) / 2;
                canvas_draw_icon(canvas, x + ICON_GAP, top, icon);
                x += icon_get_width(icon) + 2 * ICON_GAP;
                continue;
            }
        }
        run[len++] = *p;
    }
}

static const char* status_label(const App* app) {
    if(app->notice && (int32_t)(app->notice_until - furi_get_tick()) > 0) return app->notice;
    if(app->screen == ScreenPlayer && is_playing(app)) return NULL;
    if(!oc_uses_pad(app->output)) return "Speaker";
    if(!app->usb_ok) return "USB busy";
    return hori_usb_is_connected() ? "USB" : "--";
}

static void draw_header(Canvas* canvas, const App* app, const char* title) {
    const char* status = status_label(app);
    size_t status_w = 0;
    canvas_set_font(canvas, FontSecondary);
    if(status) {
        canvas_draw_str_aligned(canvas, SCREEN_W, HEADER_Y, AlignRight, AlignBottom, status);
        status_w = canvas_string_width(canvas, status) + STATUS_GAP;
    }
    canvas_set_font(canvas, FontPrimary);
    FuriString* fitted = furi_string_alloc_set(title);
    elements_string_fit_width(canvas, fitted, SCREEN_W - status_w);
    canvas_draw_str(canvas, 0, HEADER_Y, furi_string_get_cstr(fitted));
    furi_string_free(fitted);
    canvas_set_font(canvas, FontSecondary);
}

static void draw_list(
    Canvas* canvas,
    ListLabel label,
    const void* label_ctx,
    size_t count,
    int selected) {
    canvas_set_font(canvas, FontSecondary);
    int first = selected - LIST_ROWS / 2;
    if(first > (int)count - LIST_ROWS) first = (int)count - LIST_ROWS;
    if(first < 0) first = 0;

    for(int row = 0; row < LIST_ROWS && first + row < (int)count; row++) {
        int index = first + row;
        int y = LINE_Y(row + 1);
        const char* text = label((size_t)index, label_ctx);
        if(index == selected) {
            canvas_draw_box(canvas, 0, y - 8, SCREEN_W, 10);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, 2, y, text);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_str(canvas, 2, y, text);
        }
    }
}

static void draw_error(Canvas* canvas, const App* app) {
    canvas_draw_str(canvas, 0, LINE_Y(1), "Error:");
    canvas_draw_str(canvas, 0, LINE_Y(2), furi_string_get_cstr(app->error));
    draw_text(
        canvas,
        0,
        LINE_Y(4),
        app->song.channel_mask ? ARROW_UP ARROW_DOWN " file  Hold " ARROW_LEFT ARROW_RIGHT " channel" :
                                 ARROW_UP ARROW_DOWN " file");
    canvas_draw_str(canvas, 0, LINE_Y(5), "Back: menu");
}

static void draw_playing(Canvas* canvas, const App* app) {
    char line[48];
    if(app->current_note >= 0) {
        OcCombo combo;
        oc_map(app->song.notes[app->current_note].pitch + app->transpose, app->output, app->fold, &combo);
        int pitch = combo.pitch;
        char label[24];
        combo_label(app, &combo, label, sizeof(label));
        if(oc_uses_pad(app->output)) {
            snprintf(
                line,
                sizeof(line),
                "%s%d  %s",
                oc_pitch_class_name(pitch),
                pitch / 12 - 1,
                label);
        } else {
            snprintf(line, sizeof(line), "%s", label);
        }
        canvas_set_font(canvas, FontPrimary);
        draw_text(canvas, 0, LINE_Y(2), line);
        canvas_set_font(canvas, FontSecondary);

        snprintf(
            line,
            sizeof(line),
            "Note %" PRId32 "/%u",
            app->current_note + 1,
            (unsigned)app->song.count);
        canvas_draw_str(canvas, 0, LINE_Y(4), line);
    } else {
        canvas_draw_str(canvas, 0, LINE_Y(2), "...");
    }
    canvas_draw_str(canvas, 0, LINE_Y(5), "OK/Back: stop");
}

static void draw_builtin(Canvas* canvas, const App* app) {
    char line[48];
    size_t pos = 0;
    line[0] = '\0';
    for(size_t i = 0; i < app->song.count && pos + 4 < sizeof(line); i++) {
        OcCombo combo;
        char label[24];
        oc_map(app->song.notes[i].pitch, app->output, app->fold, &combo);
        combo_label(app, &combo, label, sizeof(label));
        pos += snprintf(line + pos, sizeof(line) - pos, "%s ", label);
    }
    draw_text(canvas, 0, LINE_Y(1), line);
    snprintf(line, sizeof(line), "Tempo %d%%", app->tempo_pct);
    canvas_draw_str(canvas, 0, LINE_Y(2), line);
    if(app->output == OcOutputSwitch2) {
        if(app->controlled_id == hori_usb_connection_id()) {
            canvas_draw_str(canvas, 0, LINE_Y(3), "Used the Joy-Cons?");
            canvas_draw_str(canvas, 0, LINE_Y(4), "Menu > Take control");
        } else {
            canvas_draw_str(canvas, 0, LINE_Y(4), "OK takes control first");
        }
    }
    draw_text(
        canvas, 0, LINE_Y(5), "OK play " ARROW_UP ARROW_DOWN " song " ARROW_LEFT ARROW_RIGHT " tempo");
}

static void draw_midi(Canvas* canvas, const App* app) {
    char line[48];
    snprintf(
        line,
        sizeof(line),
        "Notes %u  oct %u  bend %u",
        (unsigned)app->song.count,
        (unsigned)app->stats.moved,
        (unsigned)app->stats.bent);
    canvas_draw_str(canvas, 0, LINE_Y(1), line);

    snprintf(line, sizeof(line), "Key %+d  Tempo %d%%", app->transpose, app->tempo_pct);
    canvas_draw_str(canvas, 0, LINE_Y(2), line);

    char channel[12] = "all";
    if(app->channel >= 0) snprintf(channel, sizeof(channel), "%d", app->channel + 1);
    snprintf(
        line, sizeof(line), "Channel: %s%s", channel, app->song.truncated ? " (cut)" : "");
    canvas_draw_str(canvas, 0, LINE_Y(3), line);

    draw_text(
        canvas, 0, LINE_Y(4), "OK play " ARROW_UP ARROW_DOWN " file " ARROW_LEFT ARROW_RIGHT " tempo");
    draw_text(
        canvas, 0, LINE_Y(5), "Hold " ARROW_UP ARROW_DOWN " key " ARROW_LEFT ARROW_RIGHT " channel");
}

static void draw_callback(Canvas* canvas, void* ctx) {
    App* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(canvas);

    switch(app->screen) {
    case ScreenMenu: {
        MenuItem items[MenuCount];
        draw_header(canvas, app, "Flipparina");
        draw_list(canvas, menu_label, app, visible_menu(app, items), app->menu_index);
        break;
    }
    case ScreenSettings: {
        SettingItem items[SettingCount];
        draw_header(canvas, app, "Settings");
        draw_list(canvas, setting_label, app, visible_settings(app, items), app->settings_index);
        break;
    }
    case ScreenSongs: {
        const SongBook* book = &song_books[app->book];
        draw_header(canvas, app, book->title);
        draw_list(canvas, song_label, book, book->count, app->song_index);
        break;
    }
    case ScreenPlayer:
        draw_header(canvas, app, furi_string_get_cstr(app->title));
        if(!furi_string_empty(app->error)) {
            draw_error(canvas, app);
        } else if(is_playing(app)) {
            draw_playing(canvas, app);
        } else if(app->builtin) {
            draw_builtin(canvas, app);
        } else {
            draw_midi(canvas, app);
        }
        break;
    }

    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* input, void* ctx) {
    App* app = ctx;
    AppEvent ev = {.type = EvInput, .input = *input};
    furi_message_queue_put(app->queue, &ev, FuriWaitForever);
}

static bool move_selection(const InputEvent* in, int* index, int count) {
    if(in->type != InputTypeShort && in->type != InputTypeRepeat) return false;
    if(in->key == InputKeyUp) {
        *index = (*index + count - 1) % count;
        return true;
    }
    if(in->key == InputKeyDown) {
        *index = (*index + 1) % count;
        return true;
    }
    return false;
}

static Action input_menu(App* app, const InputEvent* in) {
    MenuItem items[MenuCount];
    int count = visible_menu(app, items);
    if(app->menu_index >= count) app->menu_index = count - 1;
    if(move_selection(in, &app->menu_index, count)) return ActionNone;
    if(in->type != InputTypeShort) return ActionNone;
    if(in->key == InputKeyBack) return ActionExit;
    if(in->key != InputKeyOk) return ActionNone;

    MenuItem item = items[app->menu_index];
    switch(item) {
    case MenuOotSongs:
    case MenuMmSongs:
        app->book = item - MenuOotSongs;
        app->song_index = 0;
        app->screen = ScreenSongs;
        return ActionNone;
    case MenuMidiFile:
        return ActionPickFile;
    case MenuTakeControl:
        return ActionTakeControl;
    case MenuSettings:
        app->settings_index = 0;
        app->screen = ScreenSettings;
        return ActionNone;
    default:
        return ActionNone;
    }
}

static int cycle(int value, int count, int dir) {
    return (value + dir + count) % count;
}

static Action input_settings(App* app, const InputEvent* in) {
    SettingItem items[SettingCount];
    int count = visible_settings(app, items);
    if(move_selection(in, &app->settings_index, count)) return ActionNone;
    if(in->type != InputTypeShort) return ActionNone;
    if(in->key == InputKeyBack) {
        MenuItem menu[MenuCount];
        int menu_count = visible_menu(app, menu);
        for(int i = 0; i < menu_count; i++) {
            if(menu[i] == MenuSettings) app->menu_index = i;
        }
        app->screen = ScreenMenu;
        return ActionNone;
    }
    int dir = in->key == InputKeyLeft ? -1 : (in->key == InputKeyOk || in->key == InputKeyRight) ? 1 : 0;
    if(!dir) return ActionNone;

    switch(items[app->settings_index]) {
    case SettingOutput:
        app->output = (OcOutput)cycle(app->output, OcOutputCount, dir);
        save_setting(OUTPUT_FILE, output_ids[app->output]);
        return ActionOutputChanged;
    case SettingOcarinaC:
        app->ocarina_c = app->ocarina_c < 0 ? (dir > 0 ? 0 : OCARINA_C_COUNT - 1) :
                                              cycle(app->ocarina_c, OCARINA_C_COUNT, dir);
        save_setting(OCARINA_C_FILE, ocarina_c_ids[app->ocarina_c]);
        return ActionNone;
    case SettingFold:
        app->fold = !app->fold;
        save_setting(FOLD_FILE, fold_ids[app->fold ? 0 : 1]);
        return ActionNone;
    default:
        return ActionNone;
    }
}

static Action input_songs(App* app, const InputEvent* in) {
    const SongBook* book = &song_books[app->book];
    if(move_selection(in, &app->song_index, (int)book->count)) return ActionNone;
    if(in->type != InputTypeShort) return ActionNone;
    if(in->key == InputKeyBack) app->screen = ScreenMenu;
    if(in->key == InputKeyOk) {
        load_builtin(app, &book->songs[app->song_index]);
        app->player_back = ScreenSongs;
        app->screen = ScreenPlayer;
    }
    return ActionNone;
}

static Action input_player(App* app, const InputEvent* in) {
    bool is_short = in->type == InputTypeShort;

    if(is_playing(app)) {
        if(is_short && (in->key == InputKeyOk || in->key == InputKeyBack)) {
            player_request_stop(app->player);
        }
        return ActionNone;
    }

    if(is_short) {
        switch(in->key) {
        case InputKeyBack:
            app->screen = app->player_back;
            return ActionNone;
        case InputKeyOk:
            if(!can_play(app)) return ActionNone;
            if(app->output == OcOutputSwitch2 && hori_usb_connection_id() != app->controlled_id)
                return ActionFirstPlay;
            if(oc_uses_pad(app->output)) return ActionAskOcarina;
            start_playback(app, false);
            return ActionNone;
        case InputKeyRight:
            change_tempo(app, TEMPO_STEP_PCT);
            return ActionNone;
        case InputKeyLeft:
            change_tempo(app, -TEMPO_STEP_PCT);
            return ActionNone;
        default:
            break;
        }
    }

    bool vertical = in->key == InputKeyUp || in->key == InputKeyDown;
    int dir = (in->key == InputKeyDown || in->key == InputKeyRight) ? 1 : -1;

    if(app->builtin) {
        const SongBook* book = &song_books[app->book];
        if(!vertical || !move_selection(in, &app->song_index, (int)book->count)) return ActionNone;
        int tempo = app->tempo_pct;
        load_builtin(app, &book->songs[app->song_index]);
        app->tempo_pct = tempo;
        return ActionNone;
    }

    bool held = in->type == InputTypeLong || in->type == InputTypeRepeat;
    if(vertical && is_short) {
        change_file(app, dir);
    } else if(vertical && held && furi_string_empty(app->error)) {
        change_transpose(app, -dir);
    } else if(!vertical && in->type == InputTypeLong && app->song.channel_mask) {
        change_channel(app, dir);
    }
    return ActionNone;
}

static Action handle_input(App* app, const InputEvent* in) {
    switch(app->screen) {
    case ScreenMenu:
        return input_menu(app, in);
    case ScreenSettings:
        return input_settings(app, in);
    case ScreenSongs:
        return input_songs(app, in);
    case ScreenPlayer:
        return input_player(app, in);
    }
    return ActionNone;
}

static bool take_control(App* app) {
    if(!oc_uses_pad(app->output)) return false;
    if(!app->usb_ok || !hori_usb_is_connected()) {
        show_notice(app, "Not connected");
        return false;
    }
    show_notice_for(app, "Wait...", 60000);
    bool ok = player_take_control();
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(ok) app->controlled_id = hori_usb_connection_id();
    app->usb_ok = hori_usb_active();
    furi_mutex_release(app->mutex);
    show_notice(app, ok ? "Ready" : "Not connected");
    return ok;
}

static void ask_ocarina(App* app) {
    DialogMessage* msg = dialog_message_alloc();
    dialog_message_set_header(msg, "Is the ocarina out?", 64, 20, AlignCenter, AlignCenter);
    dialog_message_set_text(msg, "No: Flipper takes it out", 64, 36, AlignCenter, AlignCenter);
    dialog_message_set_buttons(msg, "No", NULL, "Yes");
    view_port_enabled_set(app->view_port, false);
    DialogMessageButton answer = dialog_message_show(app->dialogs, msg);
    view_port_enabled_set(app->view_port, true);
    dialog_message_free(msg);
    if(answer == DialogMessageButtonBack) return;
    bool take_out = answer == DialogMessageButtonLeft;
    if(take_out && app->output == OcOutputN64 && app->ocarina_c < 0) {
        msg = dialog_message_alloc();
        dialog_message_set_header(msg, "Ocarina on which C?", 64, 20, AlignCenter, AlignCenter);
        dialog_message_set_text(msg, "Change it in the menu", 64, 36, AlignCenter, AlignCenter);
        dialog_message_set_buttons(msg, "Left", "Down", "Right");
        view_port_enabled_set(app->view_port, false);
        answer = dialog_message_show(app->dialogs, msg);
        view_port_enabled_set(app->view_port, true);
        dialog_message_free(msg);
        if(answer == DialogMessageButtonBack) return;
        app->ocarina_c = answer == DialogMessageButtonLeft   ? 0 :
                         answer == DialogMessageButtonCenter ? 1 :
                                                               2;
        save_setting(OCARINA_C_FILE, ocarina_c_ids[app->ocarina_c]);
    }

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    start_playback(app, take_out);
    furi_mutex_release(app->mutex);
}

static void pick_file(App* app) {
    DialogsFileBrowserOptions opts;
    dialog_file_browser_set_basic_options(&opts, ".mid", NULL);
    opts.base_path = DATA_DIR;

    FuriString* start = furi_string_alloc_set(app->path);
    if(furi_string_empty(start)) furi_string_set(start, DATA_DIR);
    FuriString* picked = furi_string_alloc();

    view_port_enabled_set(app->view_port, false);
    bool ok = dialog_file_browser_show(app->dialogs, picked, start, &opts);
    view_port_enabled_set(app->view_port, true);
    furi_string_free(start);

    if(ok) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        furi_string_set(app->path, picked);
        load_file(app, -1);
        app->player_back = ScreenMenu;
        app->screen = ScreenPlayer;
        furi_mutex_release(app->mutex);
    }
    furi_string_free(picked);
}

/* Join outside the mutex: the note callback takes it */
static void finish_playback(App* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    Player* player = app->player;
    app->player = NULL;
    app->current_note = -1;
    furi_mutex_release(app->mutex);
    if(player) player_free(player);
}

static App* app_alloc(void) {
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->tempo_pct = 100;
    app->channel = -1;
    app->current_note = -1;
    app->controlled_id = UINT32_MAX;
    app->screen = ScreenMenu;
    app->path = furi_string_alloc();
    app->title = furi_string_alloc();
    app->error = furi_string_alloc();
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->queue = furi_message_queue_alloc(QUEUE_SIZE, sizeof(AppEvent));
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, DATA_DIR);
    app->output = (OcOutput)load_setting(storage, OUTPUT_FILE, output_ids, OcOutputCount, 0);
    app->ocarina_c = load_setting(storage, OCARINA_C_FILE, ocarina_c_ids, OCARINA_C_COUNT, -1);
    app->fold = load_setting(storage, FOLD_FILE, fold_ids, 2, 1) == 0;
    furi_record_close(RECORD_STORAGE);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    update_usb(app);
    return app;
}

static void app_free(App* app) {
    finish_playback(app);
    if(app->usb_ok) hori_usb_stop();

    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DIALOGS);

    song_free(&app->song);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    furi_string_free(app->path);
    furi_string_free(app->title);
    furi_string_free(app->error);
    free(app);
}

int32_t flipparina_app(void* p) {
    UNUSED(p);
    App* app = app_alloc();

    bool running = true;
    uint32_t last_redraw = 0;
    while(running) {
        AppEvent ev;
        if(furi_message_queue_get(app->queue, &ev, IDLE_POLL_MS) != FuriStatusOk) {
            if(furi_get_tick() - last_redraw > REDRAW_MS) {
                view_port_update(app->view_port);
                last_redraw = furi_get_tick();
            }
            continue;
        }

        Action action = ActionNone;
        if(ev.type == EvPlayerDone) {
            finish_playback(app);
        } else {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            action = handle_input(app, &ev.input);
            furi_mutex_release(app->mutex);
        }

        switch(action) {
        case ActionExit:
            running = false;
            break;
        case ActionPickFile:
            pick_file(app);
            break;
        case ActionFirstPlay:
            if(take_control(app)) ask_ocarina(app);
            break;
        case ActionAskOcarina:
            ask_ocarina(app);
            break;
        case ActionTakeControl:
            take_control(app);
            break;
        case ActionOutputChanged:
            update_usb(app);
            break;
        case ActionNone:
            break;
        }
        view_port_update(app->view_port);
    }

    app_free(app);
    return 0;
}
