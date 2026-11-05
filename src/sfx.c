/*
 * Sound effects: tables for ZGDK's sequencer (zgdk/sound/sfx.h) on voice 3.
 * Voices 0-2 belong to the ZMT music (music.c), which never writes voice 3.
 */
#include "game.h"

static const sfx_seg S_KICK[]    = { SFX_SEG(WAV_TRIANGLE, 400, 200, 5), SFX_SEG(WAV_NOISE, 800, 400, 3), SFX_END };
static const sfx_seg S_FISH[]    = { SFX_SEG(WAV_SQUARE, 988, 988, 3), SFX_SEG(WAV_SQUARE, 1319, 1319, 4), SFX_END };
static const sfx_seg S_BANK[]    = { SFX_SEG(WAV_TRIANGLE, 784, 784, 4), SFX_SEG(WAV_TRIANGLE, 988, 988, 4), SFX_SEG(WAV_TRIANGLE, 1175, 1175, 4), SFX_SEG(WAV_TRIANGLE, 1568, 1568, 10), SFX_END };
static const sfx_seg S_BREATHE[] = { SFX_SEG(WAV_NOISE, 900, 1800, 10), SFX_END };
static const sfx_seg S_SCRATCH[] = { SFX_SEG(WAV_NOISE, 4000, 2500, 3), SFX_END };
static const sfx_seg S_BREAK[]   = { SFX_SEG(WAV_NOISE, 3000, 500, 14), SFX_SEG(WAV_SQUARE, 600, 1200, 8), SFX_END };
static const sfx_seg S_STING[]   = { SFX_SEG(WAV_SQUARE, 1200, 700, 8), SFX_SEG(WAV_SQUARE, 1200, 700, 8), SFX_END };
static const sfx_seg S_BITE[]    = { SFX_SEG(WAV_SAWTOOTH, 300, 90, 16), SFX_END };
static const sfx_seg S_LOW[]     = { SFX_SEG(WAV_SQUARE, 880, 880, 4), SFX_SEG(WAV_SQUARE, 660, 660, 4), SFX_END };
static const sfx_seg S_LOST[]    = { SFX_SEG(WAV_SQUARE, 700, 500, 10), SFX_SEG(WAV_SQUARE, 500, 300, 10), SFX_SEG(WAV_SQUARE, 300, 120, 24), SFX_END };
static const sfx_seg S_POCKET[]  = { SFX_SEG(WAV_TRIANGLE, 300, 900, 8), SFX_SEG(WAV_TRIANGLE, 900, 1400, 6), SFX_END };

void game_sfx_init(void) {
    sfx_init(VOICE3, VOL_75);
}

void game_sfx_kick(void)    { sfx_play_prio(S_KICK, 1); }
void game_sfx_scratch(void) { sfx_play_prio(S_SCRATCH, 0); }
void game_sfx_breathe(void) { sfx_play_prio(S_BREATHE, 0); }
void game_sfx_fish(void)    { sfx_play_prio(S_FISH, 2); }
void game_sfx_low(void)     { sfx_play_prio(S_LOW, 1); }
void game_sfx_pocket(void)  { sfx_play_prio(S_POCKET, 2); }
void game_sfx_sting(void)   { sfx_play_prio(S_STING, 2); }
void game_sfx_bite(void)    { sfx_play_prio(S_BITE, 3); }
void game_sfx_bank(void)    { sfx_play_prio(S_BANK, 3); }
void game_sfx_break(void)   { sfx_play_prio(S_BREAK, 3); }
void game_sfx_lost(void)    { sfx_play_prio(S_LOST, 4); }
