#include <zephyr/kernel.h>
#include <zmk/behavior.h>

bool kp2_alt_active = false;

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

static const struct behavior_driver_api toggle_driver_api = {
        .binding_pressed = on_toggle_pressed,
        .binding_released = on_toggle_released,
        .data_size = 0,
};

ZMK_BEHAVIOR_DEFINE(toggle, toggle_driver_api);