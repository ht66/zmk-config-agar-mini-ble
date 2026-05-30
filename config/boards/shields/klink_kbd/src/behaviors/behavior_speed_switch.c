#define DT_DRV_COMPAT zmk_behavior_speed_switch

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include "behavior_dynamic_move.h"

struct behavior_speed_switch_config {
    const struct device *target;
};

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_speed_switch_config *cfg = dev->config;
    if (cfg->target) behavior_dmm_set_active_slot(cfg->target, 1);
    return 0;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_speed_switch_config *cfg = dev->config;
    if (cfg->target) behavior_dmm_set_active_slot(cfg->target, 0);
    return 0;
}

static const struct behavior_driver_api api = {
        .binding_pressed = on_keymap_binding_pressed,
        .binding_released = on_keymap_binding_released,
};

#define SC_INST(n)                                                                                 \
    static const struct behavior_speed_switch_config sc_config_##n = {                             \
        .target = DEVICE_DT_GET(DT_INST_PHANDLE(n, target)),                                      \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &sc_config_##n, POST_KERNEL,                     \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);

DT_INST_FOREACH_STATUS_OKAY(SC_INST)