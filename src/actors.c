#include <zgdk/physics/platformer.h>
/*
 * Everything in the sea besides the seal. The seal goes down and back up all the time, so actors
 * aren't spawned once in order: each spawn of the dive is live only while its row is near the
 * camera, and is freed (and later respawned at home) when it drifts out of range. Fish that
 * were caught are marked CAUGHT in the spawn's slot.
 *
 * Fish swim to and fro and bolt away from the seal when it gets close, so a flipper kick is
 * what catches them. Capelin come in schools near the top (1 point), arctic cod lower down (2),
 * halibut lie on the sea floor (5). Jellyfish sting (air), and from the third dive leopard seals
 * patrol the deep water: a bite takes a lot of air and makes the seal drop what it caught.
 */
#include "game.h"

#define WINDOW        48       // px beyond the screen where actors stay alive
#define FLEE_DIST     40
#define LEO_SIGHT_X   96
#define LEO_SIGHT_Y   40
#define LEO_CHASE     40
#define LEO_PATROL    12

static const uint8_t F_CAPELIN[] = { SPR_CAPELIN1, SPR_CAPELIN2 };
static const uint8_t F_COD[] = { SPR_COD1, SPR_COD2 };
static const uint8_t F_HALIBUT[] = { SPR_HALIBUT1, SPR_HALIBUT2 };
static const uint8_t F_JELLY[] = { SPR_JELLY1, SPR_JELLY2 };
static const uint8_t F_LEOPARD[] = { SPR_LEOPARD1, SPR_LEOPARD2 };
static const uint8_t F_BUBBLE[] = { SPR_BUBBLE };
static const uint8_t F_SPARK[] = { SPR_SPARK };
static const uint8_t F_CHIPS[] = { SPR_ICE_CHIPS };
/* sea creatures tick their animation every other frame: rates are in pairs of frames */
static const anim_def A_CAPELIN = { F_CAPELIN, 2, 4, ANIM_LOOP };
static const anim_def A_COD = { F_COD, 2, 6, ANIM_LOOP };
static const anim_def A_HALIBUT = { F_HALIBUT, 2, 12, ANIM_LOOP };
static const anim_def A_JELLY = { F_JELLY, 2, 10, ANIM_LOOP };
static const anim_def A_LEOPARD = { F_LEOPARD, 2, 4, ANIM_LOOP };
static const anim_def A_BUBBLE = { F_BUBBLE, 1, 1, ANIM_LOOP };
static const anim_def A_SPARK = { F_SPARK, 1, 1, ANIM_LOOP };
static const anim_def A_CHIPS = { F_CHIPS, 1, 1, ANIM_LOOP };

/* per fish type: cruise speed, flee speed, points */
static const uint8_t CRUISE[] = { [E_CAPELIN] = 10, [E_COD] = 8, [E_HALIBUT] = 3 };
static const uint8_t FLEE[] = { [E_CAPELIN] = 30, [E_COD] = 24, [E_HALIBUT] = 14 };
static const uint8_t POINTS[] = { [E_CAPELIN] = 1, [E_COD] = 2, [E_HALIBUT] = 5 };

/* jellyfish bob: 32 steps of a sine, in 12.4 */
static const int8_t BOB[32] = {
    0, 3, 6, 8, 10, 12, 13, 14, 14, 14, 13, 12, 10, 8, 6, 3,
    0, -3, -6, -8, -10, -12, -13, -14, -14, -14, -13, -12, -10, -8, -6, -3,
};

/* spawn_live[i]: the entity of spawn i while it's near the camera, NULL, or CAUGHT */
#define live   spawn_live
#define CAUGHT ((game_entity_t *)1)

void actors_reset(void) {
    uint8_t i;
    for (i = 0; i < MAX_SPAWNS; i++) {
        live[i] = NULL;
    }
}

void effect_spawn(uint8_t type, int16_t px, int16_t py) {
    game_entity_t *e = physics_entities_spawn(&game_entities, type, px, py, 1, 1);
    if (!e) return;   // effects are optional
    switch (type) {
        case E_BUBBLE:
            animated_physics_entity_play(&e->entity, &A_BUBBLE);
            e->timer = 90;
            e->entity.physics.body.vy = -12;
            break;
        case E_SPARK:
            animated_physics_entity_play(&e->entity, &A_SPARK);
            e->timer = 30;
            e->entity.physics.body.vy = -8;
            break;
        case E_CHIPS:
            animated_physics_entity_play(&e->entity, &A_CHIPS);
            e->timer = 16;
            e->entity.physics.body.vy = 12;
            break;
    }
}

static game_entity_t *spawn(uint8_t i) {
    const spawn_t *s = &spawns[i];
    int16_t x = (int16_t)s->tx * 16;
    int16_t y = (int16_t)s->ty * 16;
    game_entity_t *e = NULL;
    switch (s->type) {
        case E_CAPELIN:
            e = physics_entities_spawn(&game_entities, E_CAPELIN, x + s->arg * 12, y + 4 + (s->arg & 1) * 7, 10, 5);
            if (!e) break;
            e->entity.physics.visual.ox = -1;
            e->entity.physics.visual.oy = -7;
            animated_physics_entity_play(&e->entity, &A_CAPELIN);
            break;
        case E_COD:
            e = physics_entities_spawn(&game_entities, E_COD, x + 1, y + 4, 14, 7);
            if (!e) break;
            e->entity.physics.visual.ox = -1;
            e->entity.physics.visual.oy = -4;
            animated_physics_entity_play(&e->entity, &A_COD);
            break;
        case E_HALIBUT:
            e = physics_entities_spawn(&game_entities, E_HALIBUT, x + 1, y + 9, 14, 6);
            if (!e) break;
            e->entity.physics.visual.ox = -1;
            e->entity.physics.visual.oy = -9;
            animated_physics_entity_play(&e->entity, &A_HALIBUT);
            break;
        case E_JELLY:
            e = physics_entities_spawn(&game_entities, E_JELLY, x + 3, y + 2, 10, 10);
            if (!e) break;
            e->entity.physics.visual.ox = -3;
            e->entity.physics.visual.oy = -2;
            animated_physics_entity_play(&e->entity, &A_JELLY);
            break;
        case E_LEOPARD:
            e = physics_entities_spawn(&game_entities, E_LEOPARD, x, y + 4, 14, 8);
            if (!e) break;
            e->entity.physics.visual.ox = -1;
            e->entity.physics.visual.oy = -4;
            e->entity.physics.body.vx = LEO_PATROL;
            animated_physics_entity_play(&e->entity, &A_LEOPARD);
            break;
    }
    if (e) {
        e->data = i;
        e->timer = s->type == E_LEOPARD ? 0 : (uint8_t)(i * 37);
        if (s->type <= E_HALIBUT) {
            e->entity.physics.body.vx = (i & 1) ? CRUISE[s->type] : -CRUISE[s->type];
            e->user[2] = 0;   // LANE_OK: work the lane out on its first move
        }
    }
    return e;
}

/* spawn what came near the camera, free what drifted away: an eighth of the list per frame */
static void window(uint8_t phase) {
    uint8_t i;
    int16_t top = cam.y - WINDOW;
    int16_t bottom = cam.y + 240 + WINDOW;
    for (i = phase; i < spawn_count; i += 8) {
        int16_t y = (int16_t)spawns[i].ty * 16;
        uint8_t near = y >= top && y < bottom;
        if (near && !live[i]) {
            live[i] = spawn(i);
        } else if (!near && live[i] && live[i] != CAUGHT) {
            entity_pool_free(&game_entities, live[i]);
            live[i] = NULL;
        }
    }
}

/* Fish and leopard seals only need to turn at rock: one flags lookup per axis at the leading
 * edge, instead of body_step()'s full edge walk (it was a third of the frame with 20 fish) */
static void swim_move(body_t *b) {
    int16_t nx = b->x + b->vx, ny = b->y + b->vy;
    int16_t px = PHYSICS_FP_INT(nx), py = PHYSICS_FP_INT(b->y), lead;
    b->state = 0;
    lead = b->vx > 0 ? px + b->w - 1 : px;
    if (lead < 0 || lead >= SEA_W * 16 || (tileflags_fast((uint16_t)lead, (uint16_t)(py + (b->h >> 1))) & TF_SOLID)) {
        b->state = BODY_HIT_WALL;
        nx = b->x;
        px = PHYSICS_FP_INT(nx);
    }
    if (b->vy) {
        int16_t qy = PHYSICS_FP_INT(ny);
        lead = b->vy > 0 ? qy + b->h - 1 : qy;
        if (lead < 0 || lead >= SEA_ROWS * 16 ||
            (tileflags_fast((uint16_t)(px + (b->w >> 1)), (uint16_t)lead) & TF_SOLID)) {
            ny = b->y;
            b->vy = 0;
        }
    }
    b->x = nx;
    b->y = ny;
}

static int16_t clamp16(int16_t v, int16_t lim) {
    return v > lim ? lim : (v < -lim ? -lim : v);
}

/* A cruising fish swims level between two rocks: its lane is worked out once (when it spawns or
 * calms down after fleeing) and kept in entity user bytes, so cruising costs
 * two compares instead of tile lookups. Lane ends are hitbox-left x / 2. */
#define LANE_MIN(e)   ((e)->user[0])
#define LANE_MAX(e)   ((e)->user[1])
#define LANE_OK(e)    ((e)->user[2])

static uint8_t open_at(int16_t x, int16_t y0, int16_t y1) {
    return !((tileflags_fast((uint16_t)x, (uint16_t)y0) | tileflags_fast((uint16_t)x, (uint16_t)y1)) & TF_SOLID);
}

static void lane(game_entity_t *e) {
    body_t *b = &e->entity.physics.body;
    int16_t x = PHYSICS_FP_INT(b->x), y0 = PHYSICS_FP_INT(b->y), y1 = y0 + b->h - 1, l, r;
    l = (x + (b->w >> 1)) & ~15;   // the tile under the fish's middle
    r = l;
    while (l > 0 && open_at(l - 16, y0, y1)) l -= 16;
    while (r < SEA_W * 16 - 16 && open_at(r + 16, y0, y1)) r += 16;
    LANE_MIN(e) = (uint8_t)(l >> 1);
    LANE_MAX(e) = (uint8_t)((r + 16 - b->w) >> 1);
    LANE_OK(e) = 1;
}

/* Half the fish think and move on even frames, half on odd ones, at twice the step: the school
 * still looks smooth and it halves the cost of the busiest loop in the game */
static void fish(game_entity_t *e, uint8_t seal, int16_t sx, int16_t sy) {
    body_t *b = &e->entity.physics.body;
    int16_t dx, dy, x2;
    if (seal) {
        dx = PHYSICS_FP_INT(b->x) - sx;
        dy = PHYSICS_FP_INT(b->y) - sy;
        if (dx > -FLEE_DIST && dx < FLEE_DIST && dy > -FLEE_DIST && dy < FLEE_DIST) {
            int16_t f = FLEE[e->entity.physics.visual.type] << 1;
            b->vx = dx >= 0 ? f : -f;
            b->vy = e->entity.physics.visual.type != E_HALIBUT ? (dy >= 0 ? f >> 1 : -(f >> 1)) : 0;
            swim_move(b);
            b->vx >>= 1;
            if (b->state & BODY_HIT_WALL) b->vx = -b->vx;
            if (PHYSICS_FP_INT(b->y) < ICE_BOTTOM + 4) b->y = PHYSICS_FP(ICE_BOTTOM + 4);
            LANE_OK(e) = 0;
            e->entity.physics.visual.flip = b->vx < 0 ? SPRITE_FLIP_X : 0;
            return;
        }
    }
    // cruising: level, at cruise speed, turning at the lane ends and now and then
    if (!LANE_OK(e)) {
        lane(e);
        b->vx = b->vx >= 0 ? CRUISE[e->entity.physics.visual.type] : -CRUISE[e->entity.physics.visual.type];
    }
    if (++e->timer == 0 && (rand8() & 1)) b->vx = -b->vx;
    b->x += b->vx << 1;
    x2 = PHYSICS_FP_INT(b->x) >> 1;
    if (x2 < LANE_MIN(e)) {
        b->x = PHYSICS_FP(LANE_MIN(e) << 1);
        b->vx = -b->vx;
    } else if (x2 > LANE_MAX(e)) {
        b->x = PHYSICS_FP(LANE_MAX(e) << 1);
        b->vx = -b->vx;
    }
    e->entity.physics.visual.flip = b->vx < 0 ? SPRITE_FLIP_X : 0;
}

static void leopard(game_entity_t *e, const body_t *sb) {
    body_t *b = &e->entity.physics.body;
    int16_t home_y = (int16_t)spawns[e->data].ty * 16 + 4;
    if (e->timer > 200) {
        // resting after a chase: drift back to patrol height
        e->timer--;
        b->vx = b->vx >= 0 ? LEO_PATROL : -LEO_PATROL;
        b->vy = PHYSICS_FP_INT(b->y) > home_y ? -8 : 8;
        if (e->timer == 200) e->timer = 0;
    } else if (sb && seal_alive()) {
        int16_t dx = PHYSICS_FP_INT(sb->x) - PHYSICS_FP_INT(b->x);
        int16_t dy = PHYSICS_FP_INT(sb->y) - PHYSICS_FP_INT(b->y);
        if (e->timer || (dx > -LEO_SIGHT_X && dx < LEO_SIGHT_X && dy > -LEO_SIGHT_Y && dy < LEO_SIGHT_Y)) {
            // chase for two seconds, then give up for a while
            b->vx = clamp16(b->vx + (dx > 0 ? 3 : -3), LEO_CHASE);
            b->vy = clamp16(b->vy + (dy > 0 ? 2 : -2), LEO_CHASE / 2);
            if (++e->timer >= 120) e->timer = 255;
        } else {
            b->vx = b->vx >= 0 ? LEO_PATROL : -LEO_PATROL;
            b->vy = 0;
        }
    }
    swim_move(b);
    if (b->state & BODY_HIT_WALL) b->vx = -b->vx;
    if (PHYSICS_FP_INT(b->y) < ICE_BOTTOM + 8) b->y = PHYSICS_FP(ICE_BOTTOM + 8);
    e->entity.physics.visual.flip = b->vx < 0 ? SPRITE_FLIP_X : 0;
}

/* The busiest loop in the game, so it walks the pool directly, the effects (one-frame animations,
 * no contact) take a short path, and the sea creatures move and animate on alternate frames
 * (their animation rates are halved to match) */
void actors_update(void) {
    game_entity_t *e, *end, *seal = seal_entity();
    const body_t *sb = (seal && seal_alive()) ? &seal->entity.physics.body : NULL;
    uint8_t touch, type, odd;
    int16_t sx = 0, sy = 0, sx1 = 0, sy1 = 0, ex, ey;

    // each spawn is checked every 8 frames: the camera moves at most 6 px a frame, the window is 48 px
    window(frame_count & 7);
    if (sb) {
        sx = PHYSICS_FP_INT(sb->x);
        sy = PHYSICS_FP_INT(sb->y);
        sx1 = sx + sb->w;
        sy1 = sy + sb->h;
    }
    end = pool + POOL_SIZE;
    for (e = pool; e != end; e++) {
        type = e->entity.physics.visual.type;
        if (type == ENTITY_FREE || type == E_SEAL) continue;
        if (type >= E_BUBBLE) {
            e->entity.physics.body.y += e->entity.physics.body.vy;
            if (type == E_BUBBLE) {
                e->entity.physics.body.x += (e->timer & 16) ? 4 : -4;
                if (PHYSICS_FP_INT(e->entity.physics.body.y) < ICE_BOTTOM - 4) e->timer = 1;
            }
            if (--e->timer == 0) entity_pool_free(&game_entities, e);
            continue;
        }
        touch = 0;
        if (sb) {
            // Seal bounds are fixed during this pass; seal_hurt() changes velocity, not size.
            ey = PHYSICS_FP_INT(e->entity.physics.body.y);
            if (ey < sy1 && sy < ey + e->entity.physics.body.h) {
                ex = PHYSICS_FP_INT(e->entity.physics.body.x);
                touch = ex < sx1 && sx < ex + e->entity.physics.body.w;
            }
        }
        odd = (uint8_t)(e->data ^ frame_count) & 1;
        switch (type) {
            case E_CAPELIN:
            case E_COD:
            case E_HALIBUT:
                if (touch) {
                    game.carried++;
                    game.carried_pts += POINTS[type];
                    game_sfx_fish();
                    effect_spawn(E_SPARK, PHYSICS_FP_INT(e->entity.physics.body.x), PHYSICS_FP_INT(e->entity.physics.body.y) - 6);
                    entity_pool_free(&game_entities, e);
                    live[e->data] = CAUGHT;
                    continue;
                }
                if (odd) fish(e, sb != NULL, sx, sy);
                break;
            case E_JELLY:
                e->timer++;
                e->entity.physics.body.y += BOB[(e->timer >> 2) & 31];
                if (touch) {
                    seal_hurt(PHYSICS_FP_INT(e->entity.physics.body.x) + 5, game.air_max / 6);
                    game_sfx_sting();
                }
                break;
            case E_LEOPARD:
                leopard(e, sb);
                if (touch && seal_alive()) {
                    seal_hurt(PHYSICS_FP_INT(e->entity.physics.body.x) + 7, game.air_max / 3);
                    game.carried = 0;
                    game.carried_pts = 0;
                    game_sfx_bite();
                    e->timer = 255;   // satisfied for a while
                }
                break;
        }
        if (odd) e->entity.physics.visual.sprite->tile = anim_tick(&e->entity.anim);
    }
}
