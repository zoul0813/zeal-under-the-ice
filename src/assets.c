#include <stdint.h>
#include <stddef.h>
#include <zvb_gfx.h>
#include "assets.h"

gfx_error load_palette(gfx_context* ctx) {
    const size_t size = &_tileset_ztp_end - &_tileset_ztp_start;
    return gfx_palette_load(ctx, &_tileset_ztp_start, size, 0);
}

/* Calls the LZ loader directly: gfx_tileset_load() links every decoder (~2.5 KB) */
gfx_error load_tileset(gfx_context* ctx) {
    const size_t size = &_tileset_zts_end - &_tileset_zts_start;
    return gfx_tileset_load_lz(ctx, &_tileset_zts_start, size, 0, 0, 0);
}
