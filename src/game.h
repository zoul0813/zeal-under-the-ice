#include <stdint.h>
#include <zos_errors.h>
#include <zvb_gfx.h>
/* only the ZGDK headers used: <zgdk.h> pulls rect.h/ascii.h/music.h, whose non-inline static
 * functions get compiled into every .c that includes them */
#include "zgdk/input/button_map.h"
#include "zgdk/sprites.h"
#include "zgdk/tilemap.h"
#include "zgdk/utils/rand.h"
#include "zgdk/sound/sounds.h"
#include "zgdk/sound/tracker.h"
#include <zgdk/physics/body.h>
#include <zgdk/tilemap/flags.h>
#include <zgdk/sound/sfx.h>
#include <zgdk/physics/inertial.h>
#include <zgdk/camera.h>
#include "stream.h"
#include <zgdk/input.h>
#include "gfx.h"
#include "entities.h"

#ifndef GAME_H
#define GAME_H

#define SCREEN_W       320
#define SCREEN_H       240

#define SEA_LAYER      0   // sky, water, rock and sand: streamed from the sea buffer, scrolls up and down
#define ICE_LAYER      1   // the ice sheet and the light under its holes: drifts sideways on its own

/* The sea: 20 tiles wide (exactly the screen), SEA_ROWS deep. Row 0 is sky, rows 1-2 hold the
 * ice sheet (on layer 1), the water starts behind row 2. */
#define SEA_W          20
#define SEA_ROWS       94           // 1504 px; tileflags cache in sea.c matches this height
#define ICE_TOP        16           // top of the ice (snow)
#define ICE_BOTTOM     48           // underside of the ice: the ceiling where there's no hole
#define WATERLINE      39           // the water level inside a hole
#define BREATHE_Y      34           // seal top above this, in a hole: breathing
#define HOLE_CEILING   28           // how far up a hole lets the seal come
#define ICE_COLS       80           // the ice ring: one layer-1 tilemap width, 1280 px around
#define SHAFT_ROWS     7            // light under a hole, layer-1 rows 3..9
#define ICE_SCROLL_MAX ((3 + SHAFT_ROWS) * 16)   // layer 1 stops scrolling down once the ice is off screen

/* ice ring cells */
#define ICE_SOLID      0
#define ICE_HOLE       1
#define ICE_THIN       2
#define ICE_POCKET     3            // trapped air under the ice: a gulp of air, once

#define POOL_SIZE      28
#define HUD_SPRITES    24
#define TEXT_LINES     3
#define TEXT_LEN       20
#define ARENA_SIZE     (POOL_SIZE + HUD_SPRITES + TEXT_LINES * TEXT_LEN)

/* entity types */
#define E_SEAL     0
#define E_CAPELIN  1
#define E_COD      2
#define E_HALIBUT  3
#define E_JELLY    4
#define E_LEOPARD  5
#define E_BUBBLE   6
#define E_SPARK    7
#define E_CHIPS    8

#define MAX_SPAWNS 80

/* input */
#define IN_LEFT    BUTTON_LEFT
#define IN_RIGHT   BUTTON_RIGHT
#define IN_UP      BUTTON_UP
#define IN_DOWN    BUTTON_DOWN
#define IN_KICK    BUTTON_B                // Z / Space, SNES B: flipper kick
#define IN_SCRATCH BUTTON_A                // X, SNES A: scratch at thin ice
#define IN_START   BUTTON_START            // Enter, SNES Start
#define IN_QUIT    BUTTON_SELECT           // Right Shift, SNES Select

#define LIVES       3
#define AIR_START   1200    // frames of air (20 s); bigger lungs add AIR_STEP
#define AIR_STEP    300
#define AIR_TOP     2400
#define SPEED_START 36      // swimming speed, 12.4; stronger flippers add SPEED_STEP
#define SPEED_STEP  4
#define SPEED_TOP   56
#define AIR_PIPS    8

typedef struct {
    uint8_t type, tx, ty, arg;
} spawn_t;

typedef struct {
    uint8_t dive;          // 0 based
    uint8_t lives;
    uint16_t air, air_max;
    uint8_t speed;         // swim max speed (12.4)
    uint8_t carried;       // fish caught this breath
    uint8_t carried_pts;
    uint8_t banked;        // fish banked this dive
    uint8_t quota;
    uint8_t depth;         // seal depth in m (rows under the ice)
    uint8_t floor;         // sea floor row this dive
    uint16_t score;
    uint8_t state;         // GS_*
    uint16_t timer;
} game_t;

#define GS_PLAY     0
#define GS_CLEAR    1
#define GS_DEAD     2
#define GS_OVER     3

extern gfx_context vctx;
extern game_t game;
extern camera_t cam;
extern uint16_t input_now;
extern uint16_t input_pressed;
extern uint8_t frame_count;
extern uint16_t hiscore;

/* sea.c */
zos_err_t sea_init(void);
void sea_generate(uint8_t dive);  // sea floor, rocks, ice ring, spawns
void sea_frame(void);             // camera, streaming, ice drift (before the vblank)
void sea_scroll(void);            // hardware scroll registers (inside the vblank)
uint8_t ice_at(int16_t x);        // ICE_* cell above screen/world x (the world is one screen wide)
int16_t ice_ceiling(int16_t x0, int16_t x1);   // lowest y a body spanning x0..x1 can rise to
uint8_t ice_scratch(int16_t x);   // one scratch at thin ice; returns 1 when it breaks open
void ice_take_pocket(int16_t x);
int16_t ice_nearest_hole(int16_t x);   // x of the nearest hole's center, or -1
int16_t ice_drift_px(void);       // drift this frame, in 1/16 px (the seal in a hole moves with it)
extern spawn_t *spawns;          // MAX_SPAWNS, in the dead tileset blob
extern game_entity_t **spawn_live;     // MAX_SPAWNS, the entity of each spawn (actors.c)
extern uint8_t spawn_count;

/* seal.c */
void seal_spawn_in_hole(void);
void seal_update(void);
void seal_hurt(int16_t from_x, uint16_t air_loss);
void seal_freeze(void);
uint8_t seal_alive(void);
uint8_t seal_breathing(void);
game_entity_t *seal_entity(void);

/* actors.c */
void actors_reset(void);
void actors_update(void);
void effect_spawn(uint8_t type, int16_t px, int16_t py);

/* hud.c */
zos_err_t hud_init(void);
void hud_draw(void);
void hud_render(void);              // in the vblank, after the pool's sprites
void hud_show(uint8_t on);
void text_show(uint8_t line, const char *str, uint8_t y);
void text_hide(uint8_t line);
void text_hide_all(void);
void text_number(char *buf, uint16_t n, uint8_t digits);

/* The embedded tileset blob is dead once load_tileset() has put it in VRAM (5.5 KB). It holds,
 * in order: the music's pattern buffers, the entity pool, the sea, the spawn list and each
 * spawn's entity (sea_init() checks it all fits). */
#define MUSIC_PATTERNS 3
#define MUSIC_RAM      (MUSIC_PATTERNS * 772)   // sizeof(pattern_t)
#define POOL_RAM       (POOL_SIZE * sizeof(game_entity_t))
extern game_entity_t *pool;
#define BGM_NONE     0xFF
#define BGM_TITLE    0
#define BGM_DIVE     1
#define BGM_CLEAR    2
#define BGM_OVER     3
zos_err_t bgm_init(void);
void bgm_play(uint8_t song);
void bgm_stop(void);
void bgm_tick(void);
void bgm_pause(void);
void bgm_resume(void);
void game_sfx_init(void);
void game_sfx_kick(void);
void game_sfx_fish(void);
void game_sfx_bank(void);
void game_sfx_breathe(void);
void game_sfx_scratch(void);
void game_sfx_break(void);
void game_sfx_sting(void);
void game_sfx_bite(void);
void game_sfx_low(void);
void game_sfx_lost(void);
void game_sfx_pocket(void);

#ifdef AUTOPILOT
uint16_t autopilot_input(void);
void autopilot_reset(void);
#endif

#endif
