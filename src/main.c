/*
 * Under the Ice: a seal dives from a breathing hole into the dark water under the sea ice to
 * catch fish, and has to find a hole again before its air runs out. The ice drifts, so the hole
 * it came down through isn't where it left it. Each dive goes deeper; the catch buys bigger lungs
 * or stronger flippers. Vertical diving game for Zeal 8-bit OS, built on ZGDK.
 */
#include <zos_vfs.h>
#include <stdint.h>
#include <zos_errors.h>
#include <zos_sys.h>
#include <zos_video.h>
#include "game.h"
#include "controls.h"
#include "entities.h"
#include "assets.h"
#include "zgdk/tilemap/scroll.h"
#include <zgdk/sprites.h>

#ifndef START_DIVE
#define START_DIVE 0
#endif
#define BANNER_FRAMES   150
#define CLEAR_FRAMES    150
#define RESPAWN_FRAMES  90
#define END_FRAMES      420

gfx_context vctx;
game_t game;
entity_pool_t game_entities;
uint16_t input_now;
uint16_t input_pressed;
uint8_t frame_count;
uint16_t hiscore;
static uint16_t input_last;
game_entity_t *pool;   // POOL_SIZE, in the dead tileset blob after the music (see game.h)
static gfx_sprite arena[ARENA_SIZE];
#ifdef AUTOPILOT
static uint8_t autopilot_on;
#endif

static void fatal(zos_err_t err, const char *what) {
    // printf would pull in ~2.6 KB of formatting code
    uint16_t len = 0;
    char code[4] = { ' ', '0' + (err / 10) % 10, '0' + err % 10, '\n' };
    ioctl(DEV_STDOUT, CMD_RESET_SCREEN, NULL);
    while (what[len]) len++;
    write(DEV_STDOUT, what, &len);
    len = sizeof(code);
    write(DEV_STDOUT, code, &len);
    exit(err);
}

static void read_input(void) {
    input_now = input_read();
#ifdef AUTOPILOT
    if (autopilot_on) input_now = (input_now & (IN_START | IN_QUIT)) | autopilot_input();
#endif
    input_pressed = input_now & ~input_last;
    input_last = input_now;
}

static void frame_end(void) {
    sea_frame();
    physics_entities_sync(&game_entities);
    hud_draw();
    gfx_wait_vblank(&vctx);
    sea_scroll();
    gfx_sprite_render_array(&vctx, 0, arena, POOL_SIZE);   // the entities; hud_render() does the rest
    hud_render();
    gfx_wait_end_vblank(&vctx);
    bgm_tick();
    sfx_tick();
    frame_count++;
}

static void init(void) {
    zos_err_t err;

    err = input_init(1);
    if (err) fatal(err, "input");

    gfx_enable_screen(0);
    err = gfx_initialize(ZVB_CTRL_VID_MODE_GFX_320_8BIT, &vctx);
    if (err) fatal(err, "gfx");
    err = sprites_clear(&vctx);   // a previous program may have left sprites in the table
    if (err) fatal(err, "sprites");
    err = load_palette(&vctx);
    if (err) fatal(err, "palette");
    err = load_tileset(&vctx);
    if (err) fatal(err, "tileset");
    tilemap_fill(&vctx, SEA_LAYER, BG_SEA7, 0, 0, 80, 40);

    sprites_register_arena(arena, ARENA_SIZE);
    pool = (game_entity_t *)(&_tileset_zts_start + MUSIC_RAM);
    err = sprite_entities_init(&game_entities, pool, POOL_SIZE, sizeof(*pool));
    if (err) fatal(err, "sprites");
    err = hud_init();   // after the entities: higher sprite slots draw on top of lower ones
    if (err) fatal(err, "hud");
    err = sea_init();
    if (err) fatal(err, "sea");

    sound_init_hardware();       // resets every voice, so before the tracker and the effects
    err = bgm_init();   // after load_tileset(): the music reuses the tileset blob
    if (err) fatal(err, "music");
    game_sfx_init();
    rand8_seed(0x5EA1);

    gfx_enable_screen(1);
}

static void deinit(void) {
    bgm_stop();
    sound_deinit();
    tilemap_scroll(SEA_LAYER, 0, 0);
    tilemap_scroll(ICE_LAYER, 0, 0);
    (void)sprites_clear(&vctx);   // best-effort cleanup before returning to ZOS
    ioctl(DEV_STDOUT, CMD_RESET_SCREEN, NULL);
}

/* Title over the first dive's sea, the camera sinking slowly and coming back up. Returns 0 to quit. */
static uint8_t title(void) {
    uint16_t seed = 0;
    int8_t pan = 1;
    char line[] = "BEST 00000";

    entity_pool_clear(&game_entities);
    game.dive = 0;
    sea_generate(0);
    actors_reset();
    hud_show(0);
    input_flush();
    controls_menu_begin();
    text_hide_all();
    text_show(0, "UNDER THE ICE", 40);
    text_show(1, PROMPT_START, 100);
    text_number(line + 5, hiscore, 5);
    text_show(2, hiscore ? (const char *)line : CONTROL_PROMPT("Z KICK  X SCRATCH", PAD_B " KICK  " PAD_A " SCRATCH"), 200);
    input_last = 0;
    bgm_play(BGM_TITLE);

    while (1) {
        read_input();
        if (controls_menu_update()) {
            text_show(1, PROMPT_START, 100);
            if (!hiscore) text_show(2, CONTROL_PROMPT("Z KICK  X SCRATCH", PAD_B " KICK  " PAD_A " SCRATCH"), 200);
        }
        seed++;
        if (input_pressed & IN_START) break;
        if (input_pressed & IN_QUIT) return 0;
#ifdef AUTOPILOT
        if (seed == 150) break;
#endif
        if (seed & 1) {
            cam.y += pan;
            if (cam.y >= 300) pan = -1;
            else if (cam.y <= 0) pan = 1;
        }
        actors_update();
        frame_end();
    }
    text_hide_all();
    rand8_seed(seed | 1);
    return 1;
}

static void dive_start(void) {
    char head[8] = "DIVE ";
    uint8_t n = game.dive + 1;
    char quota[] = "CATCH 00";

    entity_pool_clear(&game_entities);
    sea_generate(game.dive);
    actors_reset();
    game.quota = 6 + game.dive * 2;
    if (game.quota > 24) game.quota = 24;
    game.banked = 0;
    game.carried = 0;
    game.carried_pts = 0;
    game.air = game.air_max;
    seal_spawn_in_hole();
    if (n >= 10) {
        head[5] = '0' + n / 10;
        head[6] = '0' + n % 10;
    } else {
        head[5] = '0' + n;
    }
    text_number(quota + 6, game.quota, 2);
    text_show(0, head, 64);
    text_show(1, quota, 84);
    game.state = GS_PLAY;
    game.timer = BANNER_FRAMES;
    hud_show(1);
    bgm_play(BGM_DIVE);
#ifdef AUTOPILOT
    autopilot_reset();
#endif
}

static void pause(void) {
    text_show(2, "PAUSED", 110);
    bgm_pause();
    input_flush();
    input_last = 0;
    do {
        read_input();
        frame_end();
    } while (!(input_pressed & IN_START));
    text_hide(2);
    bgm_resume();
}

/* after a dive: pick bigger lungs or stronger flippers (only what isn't maxed out) */
static uint8_t upgrade(void) {
    uint8_t lungs = game.air_max < AIR_TOP, flips = game.speed < SPEED_TOP;
    if (!lungs && !flips) return 1;
    if (lungs) text_show(1, CONTROL_PROMPT("LEFT  BIGGER LUNGS", PAD_LEFT " BIGGER LUNGS"), 104);
    if (flips) text_show(2, CONTROL_PROMPT("RIGHT FAST FLIPPERS", PAD_RIGHT " FAST FLIPPERS"), 124);
    if ((input_pressed & IN_LEFT) && lungs) {
        game.air_max += AIR_STEP;
        return 1;
    }
    if ((input_pressed & IN_RIGHT) && flips) {
        game.speed += SPEED_STEP;
        return 1;
    }
    return 0;
}

static void play(void) {
    char line[] = "SCORE 00000";
    uint8_t choosing = 0;

    game.dive = START_DIVE;
    game.lives = LIVES;
    game.score = 0;
    game.air_max = AIR_START;
    game.speed = SPEED_START;
#ifdef AUTOPILOT
    autopilot_on = 1;
#endif
    dive_start();

    while (1) {
        read_input();
        if (input_pressed & IN_START) pause();
        if (input_pressed & IN_QUIT) break;

        if (game.state == GS_PLAY || game.state == GS_CLEAR) seal_update();
        actors_update();

        if (game.timer && --game.timer == 0 && game.state == GS_PLAY) {
            text_hide(0);
            text_hide(1);
        }

        switch (game.state) {
            case GS_CLEAR:
                if (!choosing && game.timer == 0) {
                    // just banked the last fish of the quota
                    seal_freeze();
                    choosing = 1;
                    game.timer = CLEAR_FRAMES;
                    text_show(0, "DIVE CLEAR", 64);
                    text_number(line + 6, game.score, 5);
                    text_show(1, line, 84);
                    bgm_play(BGM_CLEAR);
                } else if (choosing == 1 && game.timer == 0) {
                    choosing = 2;
                    text_hide(1);
                    text_show(0, "YOUR CATCH BUYS", 72);
                } else if (choosing == 2 && upgrade()) {
                    choosing = 0;
                    text_hide_all();
                    game.dive++;
                    dive_start();
                }
                break;
            case GS_DEAD:
                if (game.lives) game.lives--;
                if (game.lives == 0) {
                    game.state = GS_OVER;
                    game.timer = END_FRAMES;
                    text_show(0, "OUT OF AIR", 56);
                    text_number(line + 6, game.score, 5);
                    text_show(1, line, 80);
                    if (game.score > hiscore) {
                        hiscore = game.score;
                        text_show(2, "NEW BEST", 104);
                    }
                    bgm_play(BGM_OVER);
                } else {
                    game.air = game.air_max;
                    seal_spawn_in_hole();
                    text_show(0, "OUT OF AIR", 64);
                    game.timer = RESPAWN_FRAMES;
                    game.state = GS_PLAY;
                }
                break;
            case GS_OVER:
                if (game.timer == 0) goto done;
                break;
        }
        frame_end();
    }
done:
    entity_pool_clear(&game_entities);
    text_hide_all();
    hud_show(0);
    bgm_stop();
#ifdef AUTOPILOT
    autopilot_on = 0;
#endif
    frame_end();
    input_flush();
}

int main(void) {
    init();
    while (title()) play();
    deinit();
    return 0;
}
