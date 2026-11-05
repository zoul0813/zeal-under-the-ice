#include <zgdk/tilemap/flags.h>
#include <zgdk/physics/platformer.h>
/*
 * The sea and the ice over it.
 *
 * Layer 0 is the water column: a 20 x SEA_ROWS tile buffer in the dead tileset blob (sky, water
 * getting darker every 10 m, rock outcrops, the sand floor), made fresh for each dive and streamed by the shared core
 * (stream.h) as the camera goes up and down. The world is exactly one screen wide, so
 * the camera only moves vertically.
 *
 * Layer 1 is the ice sheet: an 80-cell ring (the whole 1280 px width of the tilemap) of solid ice,
 * holes, thin ice and air pockets, drawn once per dive into layer-1 rows 1-2, with light shafts in
 * rows 3-9 under every hole. It drifts by moving layer 1's horizontal scroll register only, so the
 * holes slide across the screen for free. Vertically layer 1 follows the camera until the ice is
 * off the top of the screen, then stops, so the ring rows never wrap back into view.
 */
#include "game.h"
#include "assets.h"
#include "zgdk/tilemap/scroll.h"

TILEFLAGS_DEFINE_CACHE(94);

#define SCROLL_W      1280
#define DRIFT_WRAP    ((uint16_t)SCROLL_W * 16)

camera_t cam;
spawn_t *spawns;
game_entity_t **spawn_live;
uint8_t spawn_count;
static uint8_t *sea;                 // SEA_W * SEA_ROWS, in the dead tileset blob (see game.h)
static tilemap_stream_t stream;
static Tilemap tilemap;
static uint8_t ice[ICE_COLS];
static uint8_t crack[ICE_COLS];      // scratches on thin ice
static uint16_t drift;               // layer-1 scroll, 1/16 px, wraps at DRIFT_WRAP
static int8_t drift_speed;           // 1/16 px per frame
static uint8_t row_buf[ICE_COLS];

static const uint8_t TILE_FLAGS[256] = { TILE_FLAGS_INIT };

#define AT(x, y) sea[(uint16_t)(y) * SEA_W + (x)]

/* Call after load_tileset(): the tileset blob is dead once it's in VRAM */
zos_err_t sea_init(void) {
    // the blob after the music and the entity pool: the sea, the spawn list and each spawn's entity
    sea = &_tileset_zts_start + MUSIC_RAM + POOL_RAM;
    spawns = (spawn_t *)(sea + SEA_W * SEA_ROWS);
    spawn_live = (game_entity_t **)(spawns + MAX_SPAWNS);
    if ((uint8_t *)(spawn_live + MAX_SPAWNS) > &_tileset_zts_end) return ERR_NO_MORE_MEMORY;
    tileflags_register(TILE_FLAGS);
    tileflags_set_outside(TF_SOLID);
    body_set_touch_mask(0);   // no hazard or ladder tiles: skip those probes
    tilemap_fill(&vctx, ICE_LAYER, TILE_EMPTY, 0, 0, 80, 40);
    return ERR_SUCCESS;
}

/* ------------------------------------------------------------------ the ice ring */

static uint8_t ice_tile_top(uint8_t c) {
    uint8_t l = ice[(c + ICE_COLS - 1) % ICE_COLS], r = ice[(c + 1) % ICE_COLS];
    switch (ice[c]) {
        case ICE_HOLE: return TILE_EMPTY;
        case ICE_THIN: return TILE_THIN_TOP;
    }
    if (r == ICE_HOLE) return TILE_ICE_TOP_L;
    if (l == ICE_HOLE) return TILE_ICE_TOP_R;
    return TILE_ICE_TOP;
}

static uint8_t ice_tile_bottom(uint8_t c) {
    uint8_t l = ice[(c + ICE_COLS - 1) % ICE_COLS], r = ice[(c + 1) % ICE_COLS];
    switch (ice[c]) {
        case ICE_HOLE: return TILE_EMPTY;
        case ICE_THIN: return crack[c] > 40 ? TILE_THIN_CRACK2 : (crack[c] ? TILE_THIN_CRACK1 : TILE_THIN_BOT);
        case ICE_POCKET: return TILE_POCKET;
    }
    if (r == ICE_HOLE) return TILE_ICE_BOT_L;
    if (l == ICE_HOLE) return TILE_ICE_BOT_R;
    return TILE_ICE_BOT;
}

static const uint8_t SHAFT[SHAFT_ROWS] = {
    TILE_SHAFT0, TILE_SHAFT0, TILE_SHAFT1, TILE_SHAFT1, TILE_SHAFT2, TILE_SHAFT2, TILE_SHAFT3,
};

static void ice_draw_column(uint8_t c) {
    uint8_t r;
    gfx_tilemap_place(&vctx, ice_tile_top(c), ICE_LAYER, c, 1);
    gfx_tilemap_place(&vctx, ice_tile_bottom(c), ICE_LAYER, c, 2);
    for (r = 0; r < SHAFT_ROWS; r++) {
        gfx_tilemap_place(&vctx, ice[c] == ICE_HOLE ? SHAFT[r] : TILE_EMPTY, ICE_LAYER, c, 3 + r);
    }
}

static void ice_draw(void) {
    uint8_t c, r;
    for (c = 0; c < ICE_COLS; c++) row_buf[c] = ice_tile_top(c);
    gfx_tilemap_load(&vctx, row_buf, ICE_COLS, ICE_LAYER, 0, 1);
    for (c = 0; c < ICE_COLS; c++) row_buf[c] = ice_tile_bottom(c);
    gfx_tilemap_load(&vctx, row_buf, ICE_COLS, ICE_LAYER, 0, 2);
    for (r = 0; r < SHAFT_ROWS; r++) {
        for (c = 0; c < ICE_COLS; c++) row_buf[c] = ice[c] == ICE_HOLE ? SHAFT[r] : TILE_EMPTY;
        gfx_tilemap_load(&vctx, row_buf, ICE_COLS, ICE_LAYER, 0, 3 + r);
    }
}

/* place `count` runs of `kind`, `w` cells wide, spread around the ring with solid ice between */
static void ice_place(uint8_t kind, uint8_t count, uint8_t w) {
    uint8_t n, tries, c, i, ok;
    for (n = 0; n < count; n++) {
        for (tries = 0; tries < 40; tries++) {
            c = rand8() % ICE_COLS;
            ok = 1;
            for (i = 0; i < w + 4; i++) {
                if (ice[(c + i + ICE_COLS - 2) % ICE_COLS] != ICE_SOLID) ok = 0;
            }
            if (ok) break;
        }
        for (i = 0; i < w; i++) ice[(c + i) % ICE_COLS] = kind;
    }
}

int16_t ice_drift_px(void) {
    return drift_speed;
}

static uint8_t ice_col(int16_t x) {
    uint16_t px = (uint16_t)(x + (int16_t)(drift >> 4));
    while (px >= SCROLL_W) px -= SCROLL_W;
    return (uint8_t)(px >> 4);
}

uint8_t ice_at(int16_t x) {
    return ice[ice_col(x)];
}

/* A body spanning x0..x1 under the ice: it can come up to HOLE_CEILING only if it fits in a hole */
int16_t ice_ceiling(int16_t x0, int16_t x1) {
    if (ice_at(x0) == ICE_HOLE && ice_at(x1) == ICE_HOLE) return HOLE_CEILING;
    return ICE_BOTTOM;
}

uint8_t ice_scratch(int16_t x) {
    uint8_t c = ice_col(x);
    uint8_t before;
    if (ice[c] != ICE_THIN) return 0;
    before = crack[c];
    if (++crack[c] < 80) {
        if ((before == 0) || (before == 40)) ice_draw_column(c);
        return 0;
    }
    // through: this cell and its thin neighbours open up
    ice[c] = ICE_HOLE;
    if (ice[(c + 1) % ICE_COLS] == ICE_THIN) ice[(c + 1) % ICE_COLS] = ICE_HOLE;
    if (ice[(c + ICE_COLS - 1) % ICE_COLS] == ICE_THIN) ice[(c + ICE_COLS - 1) % ICE_COLS] = ICE_HOLE;
    ice_draw_column((c + ICE_COLS - 2) % ICE_COLS);
    ice_draw_column((c + ICE_COLS - 1) % ICE_COLS);
    ice_draw_column(c);
    ice_draw_column((c + 1) % ICE_COLS);
    ice_draw_column((c + 2) % ICE_COLS);
    return 1;
}

void ice_take_pocket(int16_t x) {
    uint8_t c = ice_col(x);
    if (ice[c] != ICE_POCKET) return;
    ice[c] = ICE_SOLID;
    ice_draw_column(c);
}

/* Screen x of the center of the nearest hole that is on screen, or -1 */
int16_t ice_nearest_hole(int16_t x) {
    int16_t best = -1, bd = 0x7FFF, sx, d;
    uint8_t c, w;
    int16_t off = (int16_t)((drift >> 4) & 15);
    // walk the 21 cells that can be on screen
    for (sx = -off; sx < 320; sx += 16) {
        c = ice_col(sx);
        if (ice[c] != ICE_HOLE || ice[(c + ICE_COLS - 1) % ICE_COLS] == ICE_HOLE) continue;
        // first cell of a hole: measure its width
        for (w = 1; w < 4 && ice[(c + w) % ICE_COLS] == ICE_HOLE; w++) {}
        d = sx + (int16_t)w * 8;
        if (d < 8 || d > 312) continue;
        if ((d > x ? d - x : x - d) < bd) {
            bd = d > x ? d - x : x - d;
            best = d;
        }
    }
    return best;
}

/* ------------------------------------------------------------------ the water column */

static uint8_t water_tile(uint8_t row) {
    uint8_t d, band;
    if (row == 0) return BG_SKY;
    if (row == 1) return BG_SKY_LOW;
    if (row == 2) return BG_SURFACE;
    d = row - 3;
    band = d / 12;
    if (band >= 7) return BG_SEA7;
    // last row of a band blends into the next
    return BG_SEA0 + band * 2 + (d % 12 == 11);
}

static uint8_t is_rock(uint8_t t) {
    return t == TILE_ROCK || t == TILE_ROCK2;
}

static void add_spawn(uint8_t type, uint8_t tx, uint8_t ty, uint8_t arg) {
    if (spawn_count >= MAX_SPAWNS) return;
    spawns[spawn_count].type = type;
    spawns[spawn_count].tx = tx;
    spawns[spawn_count].ty = ty;
    spawns[spawn_count].arg = arg;
    spawn_count++;
}

static uint8_t open_water(uint8_t x, uint8_t y) {
    return !(TILE_FLAGS[AT(x, y)] & TF_SOLID);
}

void sea_generate(uint8_t dive) {
    uint8_t x, y, i, n, left = 0, right = 0, floor;
    uint8_t holes, thin, pockets;

    if (dive > 12) dive = 12;   // from here on only the score goes up

    floor = 34 + dive * 14;
    if (floor > SEA_ROWS - 3) floor = SEA_ROWS - 3;
    game.floor = floor;
    spawn_count = 0;

    // water, then walls that wander in from both sides below the first few metres
    for (y = 0; y < SEA_ROWS; y++) {
        uint8_t t = water_tile(y);
        for (x = 0; x < SEA_W; x++) AT(x, y) = t;
        if (y < 8 || y >= floor) continue;
        n = rand8();
        if ((n & 3) == 0 && left < 3) left++;
        else if ((n & 3) == 1 && left) left--;
        if ((n & 12) == 0 && right < 3) right++;
        else if ((n & 12) == 4 && right) right--;
        for (x = 0; x < left; x++) AT(x, y) = TILE_ROCK;
        for (x = 0; x < right; x++) AT(SEA_W - 1 - x, y) = TILE_ROCK;
    }
    // ledges and boulders, every few rows, alternating sides
    for (y = 12; y + 2 < floor; y += 5 + (rand8() & 3)) {
        n = rand8();
        if (n & 1) {
            uint8_t w = 3 + (n >> 5) % 4;
            uint8_t x0 = (n & 2) ? 0 : SEA_W - w;
            for (x = x0; x < x0 + w; x++) AT(x, y) = TILE_ROCK2;
        } else {
            uint8_t x0 = 5 + (n >> 4) % 9;
            AT(x0, y) = AT(x0 + 1, y) = AT(x0, y + 1) = AT(x0 + 1, y + 1) = TILE_ROCK;
        }
    }
    // the sea floor, with kelp in the deep bands
    for (y = floor; y < SEA_ROWS; y++) {
        for (x = 0; x < SEA_W; x++) AT(x, y) = y == floor ? TILE_SAND_TOP : TILE_SAND;
    }
    for (i = 0; i < 3; i++) AT(1 + rand8() % 18, floor) = TILE_SAND_STAR;
    if (floor - 3 >= 3 + 12 * 4) {
        for (i = 0; i < 6; i++) {
            x = 1 + rand8() % 18;
            n = 1 + (rand8() & 1);
            for (y = floor - 1; y >= floor - n; y--) {
                uint8_t band = (uint8_t)((y - 3) / 12);
                if (band < 4 || (y - 3) % 12 == 11 || !open_water(x, y)) break;
                AT(x, y) = BG_KELP0 + (band > 7 ? 3 : band - 4);
            }
        }
    }
    // rock edges: lit tops, dark undersides
    for (y = 3; y < floor; y++) {
        for (x = 0; x < SEA_W; x++) {
            if (!is_rock(AT(x, y))) continue;
            if (!(TILE_FLAGS[AT(x, y - 1)] & TF_SOLID)) AT(x, y) = TILE_ROCK_TOP;
            else if (!(TILE_FLAGS[AT(x, y + 1)] & TF_SOLID)) AT(x, y) = TILE_ROCK_BOT;
        }
    }

    // fish and hazards, top to bottom
    for (y = 5; y < floor - 1; y += 3) {
        uint8_t depth = y - 3;
        n = rand8();
        x = 2 + n % 16;
        if (!open_water(x, y) || !open_water(x + 1, y)) continue;
        if (depth < 30 && (n & 0x40)) {
            for (i = 0; i < 3; i++) add_spawn(E_CAPELIN, x, y, i);   // a school of three
        } else if (depth >= 14 && (n & 0x60)) {
            add_spawn(E_COD, x, y, 0);
        }
        n = rand8();
        if (depth > 8 && n < 40 + dive * 12) {
            x = 2 + (n >> 2) % 16;
            if (open_water(x, y + 1)) add_spawn(E_JELLY, x, y + 1, 0);
        }
        if (dive >= 2 && depth > 30 && n > 250 - dive * 2) {
            add_spawn(E_LEOPARD, 3, y, 0);
        }
    }
    for (i = 0; i < 2 + dive; i++) add_spawn(E_HALIBUT, 2 + rand8() % 16, floor - 1, 0);

    // the ice: fewer holes and more thin ice the further you go, and it drifts faster
    for (i = 0; i < ICE_COLS; i++) {
        ice[i] = ICE_SOLID;
        crack[i] = 0;
    }
    // holes spread evenly around the ring (with some jitter), so the next one is never more than
    // a screen or so of drift away; thin ice and air pockets go anywhere in between
    holes = dive < 2 ? 5 - dive : (dive < 6 ? 3 : 2);
    thin = 3 + (dive > 4 ? 4 : dive);
    pockets = 3;
    n = ICE_COLS / holes;
    for (i = 0; i < holes; i++) {
        x = i * n + rand8() % (n / 3);
        ice[x] = ice[x + 1] = ICE_HOLE;
    }
    ice_place(ICE_THIN, thin, 2);
    ice_place(ICE_POCKET, pockets, 1);
    drift_speed = 3 + dive;
    if (drift_speed > 10) drift_speed = 10;
    if (dive & 1) drift_speed = -drift_speed;
    // start with the first hole in the middle of the screen
    for (i = 0; i < ICE_COLS && !(ice[i] == ICE_HOLE && ice[(i + ICE_COLS - 1) % ICE_COLS] != ICE_HOLE); i++) {}
    drift = (uint16_t)(((int16_t)i * 16 + 16 - 160 + SCROLL_W) % SCROLL_W) * 16;
    ice_draw();

    camera_init_view(&cam, SEA_W * 16, SEA_ROWS * 16, SCREEN_W, SCREEN_H);
    cam.dz_top = 96;
    cam.dz_bottom = 128;
    cam.max_step = 6;
    camera_set(&cam, 0, 0);
    stream_init(&vctx, &stream, &tilemap, sea, SEA_W, SEA_ROWS, SEA_LAYER, &cam);
}

void sea_frame(void) {
    game_entity_t *seal = seal_entity();
    if (seal) {
        const body_t *body = &seal->entity.physics.body;
        camera_follow_target(&cam, PHYSICS_FP_INT(body->x), PHYSICS_FP_INT(body->y), body->w, body->h,
                             body->vx < 0 ? -1 : body->vx != 0);
    }
    tilemap_stream_update(&vctx, &stream, cam.x, cam.y);
    entity_view_set(0, cam.y);
    drift += drift_speed;
    if (drift >= DRIFT_WRAP) drift = drift_speed < 0 ? drift + DRIFT_WRAP : drift - DRIFT_WRAP;
}

void sea_scroll(void) {
    camera_scroll_layer(&cam, SEA_LAYER, 0, 0);
    tilemap_scroll(ICE_LAYER, drift >> 4, cam.y < ICE_SCROLL_MAX ? (uint16_t)cam.y : ICE_SCROLL_MAX);
}
