#define DT_DRV_COMPAT zmk_behavior_speed_switch

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include "behavior_two_axis_move.h"

struct speed_switch_config {
    const struct device *target;
};

static void set_target(const struct speed_switch_config *cfg, uint8_t slot) {
    if (cfg->target) {
        behavior_two_axis_set_active_slot(cfg->target, slot);
    }
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct speed_switch_config *cfg = dev->config;
    set_target(cfg, 1);
    return 0;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct speed_switch_config *cfg = dev->config;
    set_target(cfg, 0);
    return 0;
}

static const struct behavior_driver_api api = {
        .binding_pressed = on_keymap_binding_pressed,
        .binding_released = on_keymap_binding_released,
};

#define SPD_INST(n)                                                                                \
    static const struct speed_switch_config spd_config_##n = {                                     \
        .target = DEVICE_DT_GET(DT_INST_PHANDLE(n, target)),                                      \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &spd_config_##n, POST_KERNEL,                     \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);

DT_INST_FOREACH_STATUS_OKAY(SPD_INST)