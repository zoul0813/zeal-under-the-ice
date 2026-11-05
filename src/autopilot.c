/*
 * AUTOPILOT builds only (-DAUTOPILOT=ON): a bot that plays the real game, used to record
 * gameplay videos and to measure the frame rate under load. It returns the buttons a player
 * would hold this frame; everything else (physics, rules, sound) is the normal game.
 *
 * It hunts the nearest fish, kicking when it gets close, and heads back up once its air won't
 * cover the trip home (or it carries enough to bank). Under the ice it swims to the nearest
 * hole on screen, or to thin ice to scratch through, or waits for a hole to drift in.
 */
#include "game.h"

#define SEAL_W   12
#define SEAL_H   10

static uint8_t going_up;
static uint8_t kick_wait;
static uint8_t rest;
static uint8_t tick;
static uint8_t leave;
static uint8_t stuck, detour;   // checks without moving; frames left swimming round an obstacle
static uint8_t last_pos;           // frames left holding down out of a hole
static game_entity_t *prey;
static int16_t hole_x, thin_x, pocket_x;   // refreshed every 8 frames: the searches walk the ice

void autopilot_reset(void) {
    going_up = 0;
    kick_wait = 0;
    rest = 0;
    leave = 0;
    stuck = 0;
    detour = 0;
    prey = NULL;
}

static int16_t iabs(int16_t v) {
    return v < 0 ? -v : v;
}

/* a jellyfish or a leopard seal close by, in the way: which way to dodge (IN_UP / IN_DOWN) or 0 */
static uint16_t dodge(int16_t cx, int16_t cy, int16_t tx) {
    game_entity_t *e;
    ENTITY_POOL_FOR_EACH(&game_entities, e, entity.physics.visual.type) {
        int16_t ex, ey;
        if (e->entity.physics.visual.type != E_JELLY && e->entity.physics.visual.type != E_LEOPARD) continue;
        ex = PHYSICS_FP_INT(e->entity.physics.body.x) + 6 - cx;
        ey = PHYSICS_FP_INT(e->entity.physics.body.y) + 5 - cy;
        if (ex < -34 || ex > 34 || ey < -30 || ey > 30) continue;
        // only when it's between us and where we're going, or right on top of us
        if (iabs(ex) > 14 && ((tx > cx) != (ex > 0))) continue;
        return ey > 0 ? IN_UP : IN_DOWN;
    }
    return 0;
}

/* the nearest fish on screen, by a rough distance */
static game_entity_t *nearest_fish(int16_t cx, int16_t cy) {
    game_entity_t *e, *best = NULL;
    int16_t bd = 0x7FFF;
    ENTITY_POOL_FOR_EACH(&game_entities, e, entity.physics.visual.type) {
        int16_t d, ey;
        if (e->entity.physics.visual.type != E_CAPELIN && e->entity.physics.visual.type != E_COD && e->entity.physics.visual.type != E_HALIBUT) continue;
        ey = PHYSICS_FP_INT(e->entity.physics.body.y);
        if (ey < cam.y || ey > cam.y + 232) continue;
        d = iabs(PHYSICS_FP_INT(e->entity.physics.body.x) - cx) + iabs(ey - cy) * 2;
        if (d < bd) {
            bd = d;
            best = e;
        }
    }
    return best;
}

/* screen x of the nearest thin ice (or air pocket when short of air), or -1 */
static int16_t nearest_ice(int16_t cx, uint8_t kind) {
    int16_t sx, best = -1, bd = 0x7FFF;
    for (sx = 8; sx < 312; sx += 8) {
        if (ice_at(sx) != kind) continue;
        if (iabs(sx - cx) < bd) {
            bd = iabs(sx - cx);
            best = sx;
        }
    }
    return best;
}

static uint16_t steer(int16_t cx, int16_t cy, int16_t tx, int16_t ty, uint8_t slack) {
    uint16_t in = 0;
    if (tx > cx + slack) in |= IN_RIGHT;
    else if (tx < cx - slack) in |= IN_LEFT;
    if (ty > cy + slack) in |= IN_DOWN;
    else if (ty < cy - slack) in |= IN_UP;
    return in;
}

uint16_t autopilot_input(void) {
#ifdef AP_IDLE
    return 0;
#endif
    game_entity_t *seal = seal_entity();
    body_t *b;
    int16_t x, y, cx, cy, tx, ty, hole;
    uint16_t in = 0, need, d;
    game_entity_t *fish;

    if (kick_wait) kick_wait--;
    tick++;
    if (game.state == GS_CLEAR) {
        // the upgrade pick: lungs and flippers in turn
        if ((tick & 15) != 0) return 0;
        return (game.dive & 1) ? IN_RIGHT : IN_LEFT;
    }
    if (!seal || !seal_alive() || game.state != GS_PLAY) return 0;
    b = &seal->entity.physics.body;
    x = PHYSICS_FP_INT(b->x);
    y = PHYSICS_FP_INT(b->y);
    cx = x + SEAL_W / 2;
    cy = y + SEAL_H / 2;

    // frames of air the trip back up takes, with a margin for finding a hole
    d = y > ICE_BOTTOM ? (uint16_t)(y - ICE_BOTTOM) : 0;
    need = d * 16 / game.speed + 360;
    if (!going_up && (game.air < need || game.carried >= 6 ||
                      (game.carried && game.banked + game.carried >= game.quota))) going_up = 1;

    if (leave) {
        leave--;
        return IN_DOWN | (leave == 20 ? IN_KICK : 0);
    }
    if (seal_breathing()) {
        // fill up, then go back down
        going_up = 0;
        if (game.air < game.air_max - 12) return 0;
        if (++rest < 20) return 0;
        rest = 0;
        leave = 30;
        return IN_DOWN;
    }
    rest = 0;

    if (going_up || y < ICE_BOTTOM) {
        if ((tick & 7) == 0) {
            hole_x = ice_nearest_hole(cx);
            thin_x = nearest_ice(cx, ICE_THIN);
            pocket_x = nearest_ice(cx, ICE_POCKET);
        } else {
            // the ice drifted on meanwhile
            int16_t d = ice_drift_px() >> 4;
            if (hole_x >= 0) hole_x -= d;
            if (thin_x >= 0) thin_x -= d;
            if (pocket_x >= 0) pocket_x -= d;
        }
        hole = hole_x;
        if (hole >= 0) {
            tx = hole;
            ty = HOLE_CEILING;
            // line up under the hole before going in
            if (y < ICE_BOTTOM + 24 && iabs(tx - cx) > 3) ty = ICE_BOTTOM + 8;
            in = steer(cx, cy, tx, ty, 2);
        } else {
            tx = -1;
            if (game.air < game.air_max / 3) tx = pocket_x;
            if (tx < 0) tx = thin_x;
            if (tx >= 0) {
                ty = ICE_BOTTOM;
                in = steer(cx, cy, tx, ty + SEAL_H / 2, 3);
                if (iabs(tx - cx) <= 4 && y <= ICE_BOTTOM + 2) in = IN_UP | IN_SCRATCH;
            } else {
                // wait just under the ice for a hole to drift in
                tx = 160;
                ty = ICE_BOTTOM + 12;
                in = steer(cx, cy, tx, ty, 8);
            }
        }
        if (d > 48 && !kick_wait) {
            in |= IN_KICK;
            kick_wait = 24;
        }
    } else if (detour) {
        // stuck on rock chasing something: swim up and across for a while, then pick again
        detour--;
        tx = cx < 160 ? 300 : 20;
        ty = cy - 48;
        in = steer(cx, cy, tx, ty, 2);
        prey = NULL;
    } else {
        // pick a fish every few frames (the search walks the whole pool)
        if ((tick & 7) == 0 || !prey || prey->entity.physics.visual.type == ENTITY_FREE || prey->entity.physics.visual.type > E_HALIBUT) prey = nearest_fish(cx, cy);
        fish = prey;
        if (fish) {
            tx = PHYSICS_FP_INT(fish->entity.physics.body.x) + fish->entity.physics.body.w / 2;
            ty = PHYSICS_FP_INT(fish->entity.physics.body.y) + fish->entity.physics.body.h / 2;
            in = steer(cx, cy, tx, ty, 2);
            if (iabs(tx - cx) < 44 && iabs(ty - cy) < 28 && !kick_wait) {
                in |= IN_KICK;
                kick_wait = 22;
            }
        } else {
            // nothing in sight: go deeper
            tx = 160;
            ty = cy + 64;
            in = IN_DOWN | steer(cx, cy, tx, cy, 24);
        }
        {
            uint16_t avoid = dodge(cx, cy, tx);
            if (avoid) in = (in & ~(IN_UP | IN_DOWN)) | avoid;
        }
    }

    if ((tick & 15) == 0) {
        // same 8 px cell for 48 frames while hunting: stuck on rock
        uint8_t pos = (uint8_t)((x >> 3) ^ (y << 3));
        stuck = (pos == last_pos && !going_up) ? stuck + 1 : 0;
        last_pos = pos;
        if (stuck >= 3) detour = 50;
    }

    return in;
}
