#include <zgdk/physics/platformer.h>
/*
 * The seal. It swims in 8 directions with the ZGDK's inertial_update(), and the kick button is
 * a flipper stroke that bursts past swimming speed. Air runs down all the time under the ice and
 * only comes back in a hole (or a gulp from an air pocket). Fish caught on a dive are carried
 * until the seal breathes at a hole, which banks them.
 *
 * The ice is not part of the collision map: it lives on layer 1 and drifts, so the seal checks
 * it by hand (sea.c ice_ceiling()). Inside a hole the seal drifts along with the ice, and the
 * hole's sides hold it in; if the ice closes over it, it is pushed back under.
 *
 * One hitbox (12x10) for every pose. The sprite is 16x16 when swimming sideways and the hardware
 * 16x32 sprite when moving mostly up or down, breathing or scratching (flipped on Y to dive).
 */
#include "game.h"

#define SEAL_W        12
#define SEAL_H        10
#define KICK_PUSH     56
#define KICK_LIMIT    80
#define KICK_COOL     20
#define INVULN        60
#define DEAD_TIME     100
#define BREATHE_RATE  12          // air back per frame in a hole
#define LOW_AIR_EVERY 50

#define M_SWIM   0
#define M_DEAD   1
#define M_DONE   2

static const uint8_t F_SWIM[] = { SPR_SEAL_SWIM1, SPR_SEAL_SWIM2 };
static const uint8_t F_UP[] = { SPR_SEAL_UP1, SPR_SEAL_UP2 };
static const uint8_t F_BREATHE[] = { SPR_SEAL_BREATHE };
static const uint8_t F_SCRATCH[] = { SPR_SEAL_SCRATCH1, SPR_SEAL_SCRATCH2 };
static const uint8_t F_HURT[] = { SPR_SEAL_HURT };
static const anim_def A_SWIM = { F_SWIM, 2, 12, ANIM_LOOP };
static const anim_def A_SWIM_FAST = { F_SWIM, 2, 5, ANIM_LOOP };
static const anim_def A_UP = { F_UP, 2, 8, ANIM_LOOP };
static const anim_def A_BREATHE = { F_BREATHE, 1, 1, ANIM_LOOP };
static const anim_def A_SCRATCH = { F_SCRATCH, 2, 5, ANIM_LOOP };
static const anim_def A_HURT = { F_HURT, 1, 1, ANIM_LOOP };

static inertial_config_t swim = {
    .accel = 3,
    .drag = 2,
    .max_speed = SPEED_START,
    .drift_y = -1,             // drifts up very slowly when idle
    .glide = 1,
};

static game_entity_t *seal;
static uint8_t mode;
static uint8_t timer;
static uint8_t kick_cool;
static uint8_t invuln;
static uint8_t facing;          // 0 right, SPRITE_FLIP_X left
static uint8_t breathing;
static uint8_t scratching;
static uint8_t low_cool;

game_entity_t *seal_entity(void) {
    // after entity_pool_clear(&game_entities) the slot is free (or something else's)
    return (seal && seal->entity.physics.visual.type == E_SEAL) ? seal : NULL;
}

uint8_t seal_alive(void) {
    return mode == M_SWIM;
}

uint8_t seal_breathing(void) {
    return breathing;
}

static void set_tall(uint8_t tall) {
    // 16x16: the body sits in the middle of the tile; 16x32: centered on the hitbox
    seal->entity.physics.visual.tall = tall;
    seal->entity.physics.visual.ox = -2;
    seal->entity.physics.visual.oy = tall ? -9 : -3;
}

void seal_spawn_in_hole(void) {
    int16_t hole = ice_nearest_hole(160);
    int16_t x = hole < 0 ? 154 : hole - SEAL_W / 2;
    int16_t y = hole < 0 ? ICE_BOTTOM : HOLE_CEILING + 4;
    if (seal == NULL || seal->entity.physics.visual.type != E_SEAL) seal = physics_entities_spawn(&game_entities, E_SEAL, x, y, SEAL_W, SEAL_H);
    else platformer_set_pos(&seal->entity.physics.body, &seal->platformer, x, y);
    seal->entity.physics.body.vx = 0;
    seal->entity.physics.body.vy = 0;
    seal->entity.physics.visual.hidden = 0;
    seal->entity.physics.visual.flip = 0;
    set_tall(1);
    animated_physics_entity_play(&seal->entity, &A_BREATHE);
    mode = M_SWIM;
    timer = 0;
    kick_cool = 0;
    invuln = 0;
    facing = 0;
    breathing = 0;
    scratching = 0;
    swim.max_speed = game.speed;
    camera_set(&cam, 0, 0);
}

void seal_freeze(void) {
    mode = M_DONE;
    seal->entity.physics.body.vx = 0;
    seal->entity.physics.body.vy = 0;
}

void seal_hurt(int16_t from_x, uint16_t air_loss) {
    if (invuln || mode != M_SWIM) return;
    invuln = INVULN;
    game.air = game.air > air_loss ? game.air - air_loss : 1;
    seal->entity.physics.body.vx = PHYSICS_FP_INT(seal->entity.physics.body.x) + SEAL_W / 2 < from_x ? -48 : 48;
    seal->entity.physics.body.vy = 24;
}

static void out_of_air(void) {
    mode = M_DEAD;
    timer = DEAD_TIME;
    game.carried = 0;
    game.carried_pts = 0;
    game_sfx_lost();
    set_tall(0);
    seal->entity.physics.visual.flip = facing | SPRITE_FLIP_Y;   // belly up
    animated_physics_entity_play(&seal->entity, &A_HURT);
}

/* the seal against the drifting ice: holes let it up, their sides hold it in */
static void ice_contact(int16_t old_x) {
    body_t *b = &seal->entity.physics.body;
    int16_t x = PHYSICS_FP_INT(b->x), y = PHYSICS_FP_INT(b->y);
    int16_t ceiling = ice_ceiling(x + 1, x + SEAL_W - 2);

    if (y < ICE_BOTTOM && ceiling == ICE_BOTTOM) {
        // up inside a hole and now over ice: either it swam into the hole's side, or the hole moved
        int16_t ox = PHYSICS_FP_INT(old_x);
        if (ice_ceiling(ox + 1, ox + SEAL_W - 2) != ICE_BOTTOM) {
            b->x = old_x;
            b->vx = 0;
        } else {
            b->y = PHYSICS_FP(ICE_BOTTOM);
            if (b->vy < 0) b->vy = 0;
        }
        return;
    }
    if (y < ceiling) {
        b->y = PHYSICS_FP(ceiling);
        if (b->vy < 0) b->vy = 0;
    }
}

static void bank_fish(void) {
    if (!game.carried) return;
    game.banked += game.carried;
    game.score += game.carried_pts;
    game.carried = 0;
    game.carried_pts = 0;
    game_sfx_bank();
    effect_spawn(E_SPARK, PHYSICS_FP_INT(seal->entity.physics.body.x) - 2, HOLE_CEILING - 12);
    if (game.banked >= game.quota && game.state == GS_PLAY) {
        game.state = GS_CLEAR;
        game.timer = 0;
    }
}

void seal_update(void) {
    body_t *b = &seal->entity.physics.body;
    int8_t dx = 0, dy = 0;
    int16_t old_x, x, y, cx, ax, ay;
    uint8_t cell;

    switch (mode) {
        case M_DONE:
            seal->entity.physics.visual.sprite->tile = anim_tick(&seal->entity.anim);
            return;
        case M_DEAD:
            // limp, floating up
            b->vy = -10;
            b->y += b->vy;
            if (PHYSICS_FP_INT(b->y) < ICE_BOTTOM) b->y = PHYSICS_FP(ICE_BOTTOM);
            seal->entity.physics.visual.hidden = timer < 40 && (timer & 4);
            if (--timer == 0) {
                seal->entity.physics.visual.hidden = 1;
                game.state = GS_DEAD;
            }
            return;
    }

    if (input_now & IN_RIGHT) dx = 1;
    else if (input_now & IN_LEFT) dx = -1;
    if (input_now & IN_UP) dy = -1;
    else if (input_now & IN_DOWN) dy = 1;
    if (kick_cool) kick_cool--;

    swim.max_speed = game.speed;
    inertial_update(b, &swim, dx, dy);
    if ((input_pressed & IN_KICK) && !kick_cool) {
        // kick the way the pad points, or straight ahead
        int16_t kx = dx ? dx : (dy ? 0 : (facing ? -1 : 1));
        int16_t push = KICK_PUSH + (game.speed - SPEED_START);
        body_push(b, kx * push, dy * push, KICK_LIMIT + (game.speed - SPEED_START));
        kick_cool = KICK_COOL;
        game_sfx_kick();
        effect_spawn(E_BUBBLE, PHYSICS_FP_INT(b->x) + (facing ? 10 : -4), PHYSICS_FP_INT(b->y) - 2);
    }

    // in a hole, the ice carries the seal along
    // in a hole, the ice carries the seal along; at the edge of the screen the hole goes on without
    // it, and ice_contact() pushes it back under
    if (PHYSICS_FP_INT(b->y) < ICE_BOTTOM) {
        b->x -= ice_drift_px();
        if (b->x < 0) b->x = 0;
        else if (b->x > PHYSICS_FP(SEA_W * 16 - SEAL_W)) b->x = PHYSICS_FP(SEA_W * 16 - SEAL_W);
    }
    old_x = b->x;
    body_step(b);
    ice_contact(old_x);

    x = PHYSICS_FP_INT(b->x);
    y = PHYSICS_FP_INT(b->y);
    cx = x + SEAL_W / 2;
    cell = ice_at(cx);

    // breathe and bank the catch with the nose out of the water
    breathing = y < BREATHE_Y;
    if (breathing) {
        if (game.air < game.air_max) {
            if ((frame_count & 31) == 0) game_sfx_breathe();
            game.air = game.air + BREATHE_RATE > game.air_max ? game.air_max : game.air + BREATHE_RATE;
        }
        bank_fish();
    } else {
        if (game.air) game.air--;
        if (game.air == 0) {
            out_of_air();
            return;
        }
        if (game.air < game.air_max / 4) {
            if (++low_cool >= LOW_AIR_EVERY) {
                low_cool = 0;
                game_sfx_low();
            }
        }
    }

    // against the underside of the ice: air pockets, and scratching through thin ice
    scratching = 0;
    if (y <= ICE_BOTTOM + 2) {
        if (cell == ICE_POCKET) {
            ice_take_pocket(cx);
            game.air = game.air + game.air_max / 4 > game.air_max ? game.air_max : game.air + game.air_max / 4;
            game_sfx_pocket();
            effect_spawn(E_BUBBLE, x, y);
        } else if (cell == ICE_THIN && (input_now & IN_SCRATCH)) {
            scratching = 1;
            b->vx = 0;
            if ((frame_count & 7) == 0) effect_spawn(E_CHIPS, x - 2, y - 2);
            if ((frame_count & 15) == 0) game_sfx_scratch();
            if (ice_scratch(cx)) {
                game_sfx_break();
                effect_spawn(E_CHIPS, x - 2, y - 8);
            }
        }
    }

    game.depth = y > ICE_BOTTOM ? (uint8_t)((y - ICE_BOTTOM) >> 4) : 0;

    // pose
    if (b->vx > 4) facing = 0;
    else if (b->vx < -4) facing = SPRITE_FLIP_X;
    ax = b->vx < 0 ? -b->vx : b->vx;
    ay = b->vy < 0 ? -b->vy : b->vy;
    if (invuln > INVULN - 16) {
        set_tall(0);
        seal->entity.physics.visual.flip = facing;
        animated_physics_entity_play(&seal->entity, &A_HURT);
    } else if (scratching) {
        set_tall(1);
        seal->entity.physics.visual.flip = facing;
        animated_physics_entity_play(&seal->entity, &A_SCRATCH);
    } else if (y < ICE_BOTTOM) {
        set_tall(1);
        seal->entity.physics.visual.flip = facing;
        animated_physics_entity_play(&seal->entity, breathing ? &A_BREATHE : &A_UP);
    } else if (ay > ax + 8) {
        set_tall(1);
        seal->entity.physics.visual.flip = b->vy > 0 ? (facing | SPRITE_FLIP_Y) : facing;
        animated_physics_entity_play(&seal->entity, &A_UP);
    } else {
        set_tall(0);
        seal->entity.physics.visual.flip = facing;
        animated_physics_entity_play(&seal->entity, (ax > 44 || ay > 44) ? &A_SWIM_FAST : &A_SWIM);
    }

    if (!breathing && (frame_count & 127) == 0) effect_spawn(E_BUBBLE, x + (facing ? 0 : 8), y - 4);

    if (invuln) {
        invuln--;
        seal->entity.physics.visual.hidden = (invuln & 4) != 0;
        if (!invuln) seal->entity.physics.visual.hidden = 0;
    }
    seal->entity.physics.visual.sprite->tile = anim_tick(&seal->entity.anim);
}
