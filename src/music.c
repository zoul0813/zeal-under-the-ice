/*
 * Background music: ZMT tracks (tools/make_music.py) played with ZGDK's tracker.
 *
 * One track is loaded at a time. A pattern_t takes 772 bytes of RAM, so all tracks share four
 * pattern buffers and zmt_rom_load() reparses the embedded .zmt when the track changes. The
 * buffers live in the embedded tileset blob, which is dead once load_tileset() has put it in
 * VRAM (same trick as Bubble Keep and Rooftop Escape). Tracks use voices 0-2; voice 3 belongs to the effects.
 */
#include "game.h"
#include "assets.h"

#define MAX_PATTERNS MUSIC_PATTERNS

extern uint8_t _dive_zmt_start, _dive_zmt_end;
#ifdef AUTOPILOT
/* Bot builds record silent video and need the RAM for the bot: only the dive track is embedded
 * (it still plays during the dives, so the tracker's per-frame cost is measured) */
#define _title_zmt_start _dive_zmt_start
#define _title_zmt_end _dive_zmt_end
#define _clear_zmt_start _dive_zmt_start
#define _clear_zmt_end _dive_zmt_end
#define _over_zmt_start _dive_zmt_start
#define _over_zmt_end _dive_zmt_end
#else
extern uint8_t _title_zmt_start, _title_zmt_end;
extern uint8_t _clear_zmt_start, _clear_zmt_end;
extern uint8_t _over_zmt_start, _over_zmt_end;
#endif

typedef struct {
    const uint8_t *start;
    const uint8_t *end;
    uint8_t loop;
} song_t;

static const song_t SONGS[] = {
    [BGM_TITLE] = { &_title_zmt_start, &_title_zmt_end, 1 },
    [BGM_DIVE] = { &_dive_zmt_start, &_dive_zmt_end, 1 },
    [BGM_CLEAR] = { &_clear_zmt_start, &_clear_zmt_end, 0 },
    [BGM_OVER]  = { &_over_zmt_start, &_over_zmt_end, 0 },
};

static track_t track;
static uint8_t current = BGM_NONE;
static uint8_t playing;

/* zmt_sound_off() mutes every voice, the effects voice too */
static void sound_off(void) {
    zmt_sound_off();
    sfx_restore();
}

/* Call after load_tileset(): the tileset data is overwritten */
zos_err_t bgm_init(void) {
    uint8_t i;
    pattern_t *patterns = (pattern_t *)&_tileset_zts_start;
    if ((uint16_t)(&_tileset_zts_end - &_tileset_zts_start) < MAX_PATTERNS * sizeof(pattern_t))
        return ERR_NO_MORE_MEMORY;
    for (i = 0; i < MAX_PATTERNS; i++) track.patterns[i] = &patterns[i];
    zmt_reset(VOL_75);
    return ERR_SUCCESS;
}

void bgm_play(uint8_t song) {
    const song_t *s = &SONGS[song];
    sound_off();
    if (song != current) {
        // loading reinitialises every pattern; it only happens between screens
        if (zmt_rom_load(&track, s->start, s->end - s->start) != ERR_SUCCESS) {
            current = BGM_NONE;
            playing = 0;
            return;
        }
        current = song;
    }
    zmt_track_reset(&track, 1);
    playing = 1;
}

void bgm_stop(void) {
    playing = 0;
    sound_off();
}

/* Pause keeps the playhead; bgm_resume() carries on from the next step */
void bgm_pause(void) {
    if (!playing) return;
    playing = 2;
    sound_off();
}

void bgm_resume(void) {
    if (playing == 2) playing = 1;
}


/* Once per frame */
void bgm_tick(void) {
    if (playing != 1) return;
    // a one-shot ends when the last step has played and the playhead wraps to step 0
    if (zmt_tick(&track, 1) == 0 && !SONGS[current].loop) bgm_stop();
}
