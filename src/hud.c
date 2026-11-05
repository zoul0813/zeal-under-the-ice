/*
 * HUD and banners as sprites. Both tile layers scroll (the ice even sideways), so anything that
 * has to stay put is drawn with sprites on top: the air meter, the fish carried, fish banked out
 * of the dive's quota, the depth and the lives. Text uses the font tiles at their ASCII codes.
 */
#include "game.h"
#include <zgdk/ascii.h>
#include <core.h>
#include <zgdk/hud/sprite.h>

#define H_AIR     0    // 8 bubbles
#define H_CARRY   8    // fish icon + 2 digits
#define H_BANK    11   // hole icon + 2 digits + '/' + 2 digits
#define H_DEPTH   17   // 3 digits + 'M'
#define H_LIVES   21   // seal icon + 'x' + digit
#define HUD_Y     (4 + 16)
#define SPRITE_OFF 0

static gfx_sprite *hud;   // contiguous HUD sprites, followed by the banner rows
#define TEXT(line, i) (&hud[HUD_SPRITES + (line) * TEXT_LEN + (i)])
static uint8_t shown;
static uint8_t dirty;           // DIRTY_* parts of the arena that changed since the last hud_render()
#define DIRTY_HUD   1
#define DIRTY_TEXT  2
static uint8_t last_air = 0xFF, last_low = 0xFF, last_carry = 0xFF, last_bank = 0xFF;
static uint8_t last_quota = 0xFF, last_depth = 0xFF, last_lives = 0xFF;

static gfx_sprite *add(uint16_t x, uint8_t tile) {
    gfx_sprite s = { .y = SPRITE_OFF, .x = x + 16, .tile = tile, .flags = SPRITE_NONE, .options = SPRITE_OPTION_NONE };
    return sprites_register(s);
}

zos_err_t hud_init(void) {
    uint8_t i, j;
    gfx_sprite *last;
    // This game's font occupies tiles 32-95; unsupported characters are blank.
    mem_set(ASCII_MAP, 32, sizeof(ASCII_MAP));
    ascii_map(' ', 64, 32);

    hud = add(2, SPR_AIR);
    for (i = 1; i < AIR_PIPS; i++) add(2 + i * 11, SPR_AIR);
    add(96, SPR_FISH_ICON);
    add(112, ascii_to('0'));
    add(122, ascii_to('0'));
    add(142, SPR_HOLE_ICON);
    add(158, ascii_to('0'));
    add(168, ascii_to('0'));
    add(178, ascii_to('/'));
    add(188, ascii_to('0'));
    add(198, ascii_to('0'));
    add(222, ascii_to('0'));
    add(232, ascii_to('0'));
    add(242, ascii_to('0'));
    add(252, ascii_to('M'));
    add(272, SPR_SEAL_ICON);
    add(288, ascii_to('\\'));   // the font's times sign
    add(300, ascii_to('0'));
    for (i = H_LIVES + 3; i < HUD_SPRITES; i++) add(0, 0);
    for (j = 0; j < TEXT_LINES; j++) {
        for (i = 0; i < TEXT_LEN; i++) last = add(0, 0);
    }
    if (!last) return ERR_NO_MORE_MEMORY;
    dirty = DIRTY_HUD | DIRTY_TEXT;
    return ERR_SUCCESS;
}

/* In the vblank: only the parts that changed. Rendering the whole arena every frame (113 sprites)
 * was a quarter of a frame; the HUD changes a few times a second and the text only with banners. */
void hud_render(void) {
    if (dirty & DIRTY_HUD) gfx_sprite_render_array(&vctx, POOL_SIZE, hud, HUD_SPRITES);
    if (dirty & DIRTY_TEXT) gfx_sprite_render_array(&vctx, POOL_SIZE + HUD_SPRITES, hud + HUD_SPRITES, TEXT_LINES * TEXT_LEN);
    dirty = 0;
}

void hud_show(uint8_t on) {
    uint8_t i;
    shown = on;
    for (i = 0; i <= H_LIVES + 2; i++) hud[i].y = on ? HUD_Y : SPRITE_OFF;
    last_air = last_low = last_carry = last_bank = last_quota = last_depth = last_lives = 0xFF;
    dirty |= DIRTY_HUD;
}

static void two(uint8_t at, uint8_t n) {
    if (n > 99) n = 99;
    hud[at].tile = ascii_to('0' + n / 10);
    hud[at + 1].tile = ascii_to('0' + n % 10);
}

void hud_draw(void) {
    uint8_t i, air, low;
    if (!shown) return;
    // one bubble per eighth of the lungs, rounded up; they turn red below a quarter.
    // Every 4th frame: the divide is the dearest thing here
    if ((frame_count & 3) == 0 || last_air == 0xFF) {
        air = (uint8_t)((game.air * AIR_PIPS + game.air_max - 1) / game.air_max);   // AIR_TOP * 8 fits 16 bits
        low = game.air < game.air_max / 4;
        if (air != last_air || low != last_low) {
            last_air = air;
            last_low = low;
            dirty |= DIRTY_HUD;
            for (i = 0; i < AIR_PIPS; i++) hud[H_AIR + i].tile = i < air ? (low ? SPR_AIR_LOW : SPR_AIR) : SPR_AIR_OFF;
        }
    }
    if (game.carried != last_carry) {
        last_carry = game.carried;
        dirty |= DIRTY_HUD;
        two(H_CARRY + 1, game.carried);
    }
    if (game.banked != last_bank) {
        last_bank = game.banked;
        dirty |= DIRTY_HUD;
        two(H_BANK + 1, game.banked);
    }
    if (game.quota != last_quota) {
        last_quota = game.quota;
        dirty |= DIRTY_HUD;
        two(H_BANK + 4, game.quota);
    }
    if (game.depth != last_depth) {
        last_depth = game.depth;
        dirty |= DIRTY_HUD;
        hud[H_DEPTH].tile = ascii_to(game.depth >= 100 ? '0' + game.depth / 100 : ' ');
        hud[H_DEPTH + 1].tile = ascii_to(game.depth >= 10 ? '0' + (game.depth / 10) % 10 : ' ');
        hud[H_DEPTH + 2].tile = ascii_to('0' + game.depth % 10);
    }
    if (game.lives != last_lives) {
        last_lives = game.lives;
        dirty |= DIRTY_HUD;
        hud[H_LIVES + 2].tile = ascii_to('0' + game.lives);
    }
}

/* n as `digits` decimal digits, leading zeros */
void text_number(char *buf, uint16_t n, uint8_t digits) {
    while (digits--) {
        buf[digits] = '0' + n % 10;
        n /= 10;
    }
}

void text_show(uint8_t line, const char *str, uint8_t y) {
    hud_sprite_text_t row = { TEXT(line, 0), TEXT_LEN, 12 };
    dirty |= DIRTY_TEXT;
    // Preserve this font's visual centering (12 px advance, 2 px correction).
    hud_sprite_put_s_centered(&row, str, 158, y);
}

void text_hide(uint8_t line) {
    dirty |= DIRTY_TEXT;
    hud_sprite_hide(TEXT(line, 0), TEXT_LEN);
}

void text_hide_all(void) {
    dirty |= DIRTY_TEXT;
    hud_sprite_hide(TEXT(0, 0), TEXT_LINES * TEXT_LEN);
}
