#include <zephyr/kernel.h>
#include <zmk/behavior.h>

bool kp2_alt_active = false;

// 前向声明 kp2 提供的刷新接口
extern void kp2_refresh_all(uint32_t timestamp);

static int on_toggle_pressed(struct zmk_behavior_binding *binding,
                             struct zmk_behavior_binding_event event) {
    kp2_alt_active = true;
    kp2_refresh_all(event.timestamp);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_toggle_released(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    kp2_alt_active = false;
    kp2_refresh_all(event.timestamp);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct zmk_behavior_behavior_slice toggle_slices[] = {
        ZMK_BEHAVIOR_SLICE(ON_PRESS, on_toggle_pressed),
        ZMK_BEHAVIOR_SLICE(ON_RELEASE, on_toggle_released),
};

static const struct zmk_behavior_behavior toggle_behavior = {
        .name = "TOGGLE",
        .slices = toggle_slices,
        .slices_count = ARRAY_SIZE(toggle_slices),
};

ZMK_BEHAVIOR_DEFINE(toggle, toggle_behavior);