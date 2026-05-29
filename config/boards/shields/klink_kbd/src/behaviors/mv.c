#define DT_DRV_COMPAT zmk_behavior_mv

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/hid.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

extern bool kp2_alt_active;

enum mv_dir {
    MV_UP    = 0,
    MV_DOWN  = 1,
    MV_LEFT  = 2,
    MV_RIGHT = 3,
};

struct mv_state {
    bool active;
    struct k_timer timer;
    uint8_t dir;
    int8_t fast_speed;
    int8_t slow_speed;
};

#ifndef ZMK_KEYMAP_LEN
#define ZMK_KEYMAP_LEN 128
#endif

static struct mv_state states[ZMK_KEYMAP_LEN];

static void mv_timer_handler(struct k_timer *timer) {
    struct mv_state *state = CONTAINER_OF(timer, struct mv_state, timer);
    if (!state->active) return;

    int8_t speed = kp2_alt_active ? state->slow_speed : state->fast_speed;
    int16_t dx = 0, dy = 0;  // 改为 int16_t

    switch (state->dir) {
        case MV_UP:    dy = -speed; break;
        case MV_DOWN:  dy =  speed; break;
        case MV_LEFT:  dx = -speed; break;
        case MV_RIGHT: dx =  speed; break;
    }

    // 使用正确的结构体成员名
    struct zmk_hid_mouse_report_body report = {
            .d_x = dx,
            .d_y = dy,
    };

    zmk_hid_mouse_report_set(&report);
}

static int on_mv_pressed(struct zmk_behavior_binding *binding,
                         struct zmk_behavior_binding_event event) {
    uint32_t pos = event.position;
    if (pos >= ZMK_KEYMAP_LEN) return -EINVAL;

    struct mv_state *state = &states[pos];
    state->active = true;
    state->dir = (binding->param1 >> 8) & 0xFF;
    state->fast_speed = (int8_t)(binding->param1 & 0xFF);
    state->slow_speed = (int8_t)(binding->param2 & 0xFF);

    k_timer_start(&state->timer, K_MSEC(12), K_MSEC(12));
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_mv_released(struct zmk_behavior_binding *binding,
                          struct zmk_behavior_binding_event event) {
    uint32_t pos = event.position;
    if (pos >= ZMK_KEYMAP_LEN) return -EINVAL;

    struct mv_state *state = &states[pos];
    state->active = false;
    k_timer_stop(&state->timer);

    struct zmk_hid_mouse_report_body stop = {0};
    zmk_hid_mouse_report_set(&stop);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api mv_driver_api = {
        .binding_pressed = on_mv_pressed,
        .binding_released = on_mv_released,
};

#define MV_INST(n) \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL, \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &mv_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MV_INST)