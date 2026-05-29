#define DT_DRV_COMPAT zmk_behavior_toggle

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

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
};

#define TOGGLE_INST(n) \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL, \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &toggle_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TOGGLE_INST)