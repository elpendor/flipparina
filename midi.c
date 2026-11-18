#include "midi.h"

#include <stdlib.h>
#include <string.h>

#define MAX_TEMPOS   256
#define DRUM_CHANNEL 9
#define NO_NOTE      0xFFFFFFFFu
#define RAM_RESERVE  (12 * 1024)

typedef struct {
    uint32_t start_tick;
    uint16_t dur_ticks;
    uint8_t pitch;
    uint8_t vibrato;
} TickNote;

typedef union {
    TickNote tick;
    MidiNote ms;
} NoteSlot;

_Static_assert(sizeof(NoteSlot) == sizeof(MidiNote), "NoteSlot must match MidiNote");

typedef struct {
    File* file;
    uint8_t buf[512];
    size_t len;
    size_t pos;
    uint32_t base;
} Reader;

static void rd_seek(Reader* r, uint32_t offset) {
    storage_file_seek(r->file, offset, true);
    r->base = offset;
    r->len = r->pos = 0;
}

static uint32_t rd_tell(const Reader* r) {
    return r->base + (uint32_t)r->pos;
}

static bool rd_byte(Reader* r, uint8_t* out) {
    if(r->pos >= r->len) {
        r->base += (uint32_t)r->len;
        r->len = storage_file_read(r->file, r->buf, sizeof(r->buf));
        r->pos = 0;
        if(r->len == 0) return false;
    }
    *out = r->buf[r->pos++];
    return true;
}

static bool rd_bytes(Reader* r, uint8_t* out, size_t n) {
    for(size_t i = 0; i < n; i++)
        if(!rd_byte(r, &out[i])) return false;
    return true;
}

static void rd_skip(Reader* r, uint32_t n) {
    if(r->pos + n <= r->len) {
        r->pos += n;
    } else {
        rd_seek(r, rd_tell(r) + n);
    }
}

static bool rd_varlen(Reader* r, uint32_t* out) {
    uint32_t v = 0;
    for(int i = 0; i < 4; i++) {
        uint8_t b;
        if(!rd_byte(r, &b)) return false;
        v = (v << 7) | (b & 0x7F);
        if(!(b & 0x80)) {
            *out = v;
            return true;
        }
    }
    return false;
}

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

typedef struct {
    int channel;
    uint32_t* active;
    NoteSlot* notes;
    size_t capacity;
    size_t count;
    bool collect_tempos;
    uint32_t tempo_tick[MAX_TEMPOS];
    uint32_t tempo_uspq[MAX_TEMPOS];
    size_t tempo_count;
    uint16_t channel_mask;
    uint8_t modulation[16];
    /* Highest modulation seen while each active note sounds */
    uint8_t active_vibrato[16 * 128];
} Parser;

static void emit_note(Parser* p, uint32_t start, uint32_t end, uint8_t pitch, uint8_t vibrato) {
    if(end <= start) return;
    if(p->notes && p->count < p->capacity) {
        uint32_t dur = end - start;
        p->notes[p->count].tick = (TickNote){
            .start_tick = start,
            .dur_ticks = (uint16_t)(dur > 0xFFFF ? 0xFFFF : dur),
            .pitch = pitch,
            .vibrato = vibrato,
        };
    }
    p->count++;
}

static void parse_track(Reader* r, uint32_t end, Parser* p) {
    for(size_t i = 0; i < 16 * 128; i++)
        p->active[i] = NO_NOTE;
    memset(p->modulation, 0, sizeof(p->modulation));

    uint32_t tick = 0;
    uint8_t running = 0;

    while(rd_tell(r) < end) {
        uint32_t delta;
        if(!rd_varlen(r, &delta)) return;
        tick += delta;

        uint8_t b;
        if(!rd_byte(r, &b)) return;

        if(b == 0xFF) {
            uint8_t type;
            uint32_t len;
            if(!rd_byte(r, &type) || !rd_varlen(r, &len)) return;
            if(type == 0x2F) return;
            if(type == 0x51 && len == 3) {
                uint8_t t[3];
                if(!rd_bytes(r, t, 3)) return;
                if(p->collect_tempos && p->tempo_count < MAX_TEMPOS) {
                    p->tempo_tick[p->tempo_count] = tick;
                    p->tempo_uspq[p->tempo_count] =
                        ((uint32_t)t[0] << 16) | ((uint32_t)t[1] << 8) | t[2];
                    p->tempo_count++;
                }
            } else {
                rd_skip(r, len);
            }
            continue;
        }
        if(b == 0xF0 || b == 0xF7) {
            uint32_t len;
            if(!rd_varlen(r, &len)) return;
            rd_skip(r, len);
            continue;
        }

        uint8_t d1;
        if(b & 0x80) {
            running = b;
            if(!rd_byte(r, &d1)) return;
        } else {
            if(!running) return;
            d1 = b;
        }
        uint8_t status = running & 0xF0;
        uint8_t ch = running & 0x0F;
        uint8_t d2 = 0;
        if(status != 0xC0 && status != 0xD0) {
            if(!rd_byte(r, &d2)) return;
        }

        if(ch == DRUM_CHANNEL) continue;
        if(status == 0xB0 && d1 == 1) {
            p->modulation[ch] = d2 & 0x7F;
            for(size_t k = (size_t)ch * 128; k < (size_t)ch * 128 + 128; k++) {
                if(p->active[k] != NO_NOTE && p->active_vibrato[k] < p->modulation[ch])
                    p->active_vibrato[k] = p->modulation[ch];
            }
            continue;
        }
        bool on = (status == 0x90 && d2 > 0);
        bool off = (status == 0x80) || (status == 0x90 && d2 == 0);
        if(!on && !off) continue;

        uint8_t pitch = d1 & 0x7F;
        if(on) p->channel_mask |= (uint16_t)(1u << ch);
        if(p->channel >= 0 && ch != p->channel) continue;

        size_t idx = (size_t)ch * 128 + pitch;
        if(p->active[idx] != NO_NOTE) {
            emit_note(p, p->active[idx], tick, pitch, p->active_vibrato[idx]);
            p->active[idx] = NO_NOTE;
        }
        if(on) {
            p->active[idx] = tick;
            p->active_vibrato[idx] = p->modulation[ch];
        }
    }
}

static uint16_t parse_file(Reader* r, Parser* p, FuriString* error) {
    rd_seek(r, 0);
    uint8_t hdr[14];
    if(!rd_bytes(r, hdr, 14) || memcmp(hdr, "MThd", 4) != 0) {
        furi_string_set(error, "Not a MIDI file");
        return 0;
    }
    uint32_t header_len = be32(hdr + 4);
    uint16_t ntracks = (uint16_t)((hdr[10] << 8) | hdr[11]);
    uint16_t division = (uint16_t)((hdr[12] << 8) | hdr[13]);
    if(division == 0) {
        furi_string_set(error, "Bad MIDI timing");
        return 0;
    }
    rd_seek(r, 8 + header_len);

    for(uint16_t found = 0; found < ntracks;) {
        uint8_t chunk[8];
        if(!rd_bytes(r, chunk, 8)) break;
        uint32_t len = be32(chunk + 4);
        uint32_t end = rd_tell(r) + len;
        if(memcmp(chunk, "MTrk", 4) == 0) {
            found++;
            parse_track(r, end, p);
        }
        rd_seek(r, end);
    }
    return division;
}

static bool note_less(const NoteSlot* a, const NoteSlot* b) {
    if(a->tick.start_tick != b->tick.start_tick) return a->tick.start_tick < b->tick.start_tick;
    return a->tick.pitch > b->tick.pitch;
}

static void sift_down(NoteSlot* a, size_t root, size_t n) {
    while(true) {
        size_t child = 2 * root + 1;
        if(child >= n) return;
        if(child + 1 < n && note_less(&a[child], &a[child + 1])) child++;
        if(!note_less(&a[root], &a[child])) return;
        NoteSlot t = a[root];
        a[root] = a[child];
        a[child] = t;
        root = child;
    }
}

static void heap_sort(NoteSlot* a, size_t n) {
    if(n < 2) return;
    for(size_t i = n / 2; i-- > 0;)
        sift_down(a, i, n);
    for(size_t end = n - 1; end > 0; end--) {
        NoteSlot t = a[0];
        a[0] = a[end];
        a[end] = t;
        sift_down(a, 0, end);
    }
}

static uint32_t tick_to_ms(uint32_t tick, uint16_t division, const Parser* p) {
    if(division & 0x8000) {
        uint32_t fps = (uint32_t)(-(int8_t)(division >> 8));
        uint32_t tpf = division & 0xFF;
        if(fps == 0 || tpf == 0) return 0;
        return (uint32_t)((uint64_t)tick * 1000 / (fps * tpf));
    }
    uint64_t us = 0;
    uint32_t last_tick = 0;
    uint32_t uspq = 500000;
    for(size_t i = 0; i < p->tempo_count; i++) {
        if(p->tempo_tick[i] >= tick) break;
        us += (uint64_t)(p->tempo_tick[i] - last_tick) * uspq / division;
        last_tick = p->tempo_tick[i];
        uspq = p->tempo_uspq[i];
    }
    us += (uint64_t)(tick - last_tick) * uspq / division;
    return (uint32_t)(us / 1000);
}

static void sort_tempos(Parser* p) {
    for(size_t i = 1; i < p->tempo_count; i++) {
        uint32_t t = p->tempo_tick[i], u = p->tempo_uspq[i];
        size_t j = i;
        while(j > 0 && p->tempo_tick[j - 1] > t) {
            p->tempo_tick[j] = p->tempo_tick[j - 1];
            p->tempo_uspq[j] = p->tempo_uspq[j - 1];
            j--;
        }
        p->tempo_tick[j] = t;
        p->tempo_uspq[j] = u;
    }
}

bool midi_load_file(
    Storage* storage,
    const char* path,
    int channel,
    MidiSong* song,
    FuriString* error) {
    memset(song, 0, sizeof(*song));
    bool ok = false;

    Reader* r = malloc(sizeof(Reader));
    Parser* p = malloc(sizeof(Parser));
    if(!r || !p) {
        free(r);
        free(p);
        furi_string_set(error, "Out of memory");
        return false;
    }
    memset(p, 0, sizeof(Parser));
    memset(r, 0, sizeof(Reader));
    p->channel = channel;
    p->active = malloc(sizeof(uint32_t) * 16 * 128);
    r->file = storage_file_alloc(storage);

    do {
        if(!p->active) {
            furi_string_set(error, "Out of memory");
            break;
        }
        if(!storage_file_open(r->file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
            furi_string_set(error, "Can't open file");
            break;
        }

        p->collect_tempos = true;
        uint16_t division = parse_file(r, p, error);
        if(!division) break;
        song->channel_mask = p->channel_mask;
        if(p->count == 0) {
            furi_string_set(error, channel >= 0 ? "No notes on this channel" : "No notes found");
            break;
        }
        sort_tempos(p);

        size_t want = p->count;
        size_t max_block = memmgr_heap_get_max_free_block();
        size_t fit = max_block > RAM_RESERVE ? (max_block - RAM_RESERVE) / sizeof(NoteSlot) : 0;
        p->capacity = want < fit ? want : fit;
        /* Short files are fine; only fail when RAM truncates a song to almost nothing */
        if(p->capacity < want && p->capacity < 16) {
            furi_string_set(error, "Out of memory");
            break;
        }
        p->notes = malloc(sizeof(NoteSlot) * p->capacity);
        if(!p->notes) {
            furi_string_set(error, "Out of memory");
            break;
        }

        p->collect_tempos = false;
        p->count = 0;
        parse_file(r, p, error);
        size_t n = p->count < p->capacity ? p->count : p->capacity;
        song->truncated = p->count > p->capacity;
        free(p->active);
        p->active = NULL;

        NoteSlot* a = p->notes;
        heap_sort(a, n);
        size_t m = 0;
        /* Skyline: a lower note never interrupts a higher one that is still sounding */
        for(size_t i = 0; i < n; i++) {
            if(m > 0) {
                TickNote prev = a[m - 1].tick;
                TickNote cur = a[i].tick;
                if(cur.start_tick == prev.start_tick) continue;
                if(cur.start_tick < prev.start_tick + prev.dur_ticks && cur.pitch < prev.pitch)
                    continue;
            }
            a[m++] = a[i];
        }

        /* Slot i is overwritten only after slot i+1 was read */
        for(size_t i = 0; i < m; i++) {
            TickNote t = a[i].tick;
            uint32_t end = t.start_tick + t.dur_ticks;
            if(i + 1 < m && end > a[i + 1].tick.start_tick) end = a[i + 1].tick.start_tick;
            uint32_t start_ms = tick_to_ms(t.start_tick, division, p);
            uint32_t end_ms = tick_to_ms(end, division, p);
            uint32_t dur_ms = end_ms > start_ms ? end_ms - start_ms : 1;
            a[i].ms = (MidiNote){
                .start_ms = start_ms,
                .dur_ms = (uint16_t)(dur_ms > 0xFFFF ? 0xFFFF : dur_ms),
                .pitch = t.pitch,
                .vibrato = t.vibrato,
            };
        }
        uint32_t offset = a[0].ms.start_ms;
        for(size_t i = 0; i < m; i++)
            a[i].ms.start_ms -= offset;

        NoteSlot* shrunk = realloc(a, sizeof(NoteSlot) * m);
        song->notes = (MidiNote*)(shrunk ? shrunk : a);
        p->notes = NULL;
        song->count = m;
        ok = true;
    } while(false);

    storage_file_close(r->file);
    storage_file_free(r->file);
    free(p->active);
    free(p->notes);
    free(p);
    free(r);
    return ok;
}
