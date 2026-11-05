#include "stream.h"
#include <zgdk/tilemap/flags.h>

zos_err_t stream_init(gfx_context *ctx, tilemap_stream_t *s, Tilemap *tilemap, uint8_t *tiles,
                      uint8_t width, uint8_t height, uint8_t layer, const camera_t *camera) {
    zos_err_t err;
    tilemap->rect.x = tilemap->rect.y = 0;
    tilemap->rect.w = width; tilemap->rect.h = height;
    err = tilemap_register(tilemap, tiles);
    if (err != ERR_SUCCESS) return err;
    tileflags_refresh();
    return tilemap_stream_init_ram(ctx, s, tiles, width, height, layer,
                                   camera->view_w, camera->view_h, camera->x, camera->y);
}
