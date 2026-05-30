#define DT_DRV_COMPAT zmk_behavior_speed_switch

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zephyr/sys/util.h>   // 提供 UTIL_LISTIFY
#include "behavior_two_axis_move.h"

#define MAX_TARGETS 4

struct speed_switch_config {
    const struct device *targets[MAX_TARGETS];
    int num_targets;
};

static void set_all_targets(const struct speed_switch_config *cfg, uint8_t slot) {
    for (int i = 0; i < cfg->num_targets; i++) {
        if (cfg->targets[i]) {
            behavior_two_axis_set_active_slot(cfg->targets[i], slot);
        }
    }
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct speed_switch_config *cfg = dev->config;
    set_all_targets(cfg, 1);
    return 0;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct speed_switch_config *cfg = dev->config;
    set_all_targets(cfg, 0);
    return 0;
}

static const struct behavior_driver_api api = {
        .binding_pressed = on_keymap_binding_pressed,
        .binding_released = on_keymap_binding_released,
};

// 编译时生成目标条目初始化
#define SPD_TARGET_ENTRY(n, i, _) DEVICE_DT_GET(DT_INST_PHANDLE_BY_IDX(n, targets, i)),

#define SPD_INST(n)                                                                                \
    static const struct speed_switch_config spd_config_##n = {                                     \
        .targets = {                                                                               \
            UTIL_LISTIFY(DT_INST_PROP_LEN(n, targets), SPD_TARGET_ENTRY, n)                        \
        },                                                                                         \
        .num_targets = DT_INST_PROP_LEN(n, targets),                                               \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &spd_config_##n, POST_KERNEL,                     \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);

DT_INST_FOREACH_STATUS_OKAY(SPD_INST)