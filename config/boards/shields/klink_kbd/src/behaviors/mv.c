#define DT_DRV_COMPAT zmk_behavior_mv

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zmk/behavior.h>

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
    const struct device *mouse_dev;
};

#ifndef ZMK_KEYMAP_LEN
#define ZMK_KEYMAP_LEN 128
#endif

static struct mv_state states[ZMK_KEYMAP_LEN];

// 定时器回调
static void mv_timer_handler(struct k_timer *timer) {
    struct mv_state *state = CONTAINER_OF(timer, struct mv_state, timer);
    if (!state->active || !state->mouse_dev) return;

    int8_t speed = kp2_alt_active ? state->slow_speed : state->fast_speed;
    int16_t dx = 0, dy = 0;

    switch (state->dir) {
        case MV_UP:    dy = -speed; break;
        case MV_DOWN:  dy =  speed; break;
        case MV_LEFT:  dx = -speed; break;
        case MV_RIGHT: dx =  speed; break;
    }

    if (dx != 0) {
        input_report_rel(state->mouse_dev, INPUT_REL_X, dx, false, K_NO_WAIT);
    }
    if (dy != 0) {
        input_report_rel(state->mouse_dev, INPUT_REL_Y, dy, true, K_NO_WAIT);
    }
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

    // 直接获取鼠标设备，不再需要辅助函数
    state->mouse_dev = device_get_binding("ZMK_MOUSE");
    if (state->mouse_dev) {
        k_timer_start(&state->timer, K_MSEC(12), K_MSEC(12));
    } else {
        LOG_WRN("Mouse device not found");
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_mv_released(struct zmk_behavior_binding *binding,
                          struct zmk_behavior_binding_event event) {
    uint32_t pos = event.position;
    if (pos >= ZMK_KEYMAP_LEN) return -EINVAL;

    struct mv_state *state = &states[pos];
    state->active = false;
    k_timer_stop(&state->timer);

    // 停止移动
    if (state->mouse_dev) {
        input_report_rel(state->mouse_dev, INPUT_REL_X, 0, false, K_NO_WAIT);
        input_report_rel(state->mouse_dev, INPUT_REL_Y, 0, true, K_NO_WAIT);
    }
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