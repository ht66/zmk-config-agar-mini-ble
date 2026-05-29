#define DT_DRV_COMPAT zmk_behavior_speed_modifier

#include <zephyr/device.h>
#include <zephyr/drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>

#include "behavior_dynamic_mouse_move.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct speed_modifier_config {
    const struct device *target;   /* 指向要控制的 dmmv 实例 */
    int slow_factor;             /* 减速系数 */
};

static int on_speed_modifier_press(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_device(binding->behavior_dev);
    const struct speed_modifier_config *cfg = dev->config;
    if (cfg->target) {
        /* 设备树存的是整数百分比，转换为浮点因子 */
        float factor = (float)cfg->slow_factor / 100.0f;
        dyn_mmv_set_speed_factor(cfg->target, factor);
    }
    return 0;
}

static int on_speed_modifier_release(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_device(binding->behavior_dev);
    const struct speed_modifier_config *cfg = dev->config;
    if (cfg->target) {
        dyn_mmv_set_speed_factor(cfg->target, 1.0f);
    }
    return 0;
}

static const struct behavior_driver_api speed_mod_api = {
        .binding_pressed = on_speed_modifier_press,
        .binding_released = on_speed_modifier_release,
};

#define SPEED_MOD_INST(n)                                                      \
    static const struct speed_modifier_config speed_mod_cfg_##n = {            \
        .target = DEVICE_DT_GET(DT_INST_PHANDLE(n, target)),                   \
        .slow_factor = DT_INST_PROP(n, slow_factor),                           \
    };                                                                         \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, &speed_mod_cfg_##n,             \
                          APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,    \
                          &speed_mod_api);

DT_INST_FOREACH_STATUS_OKAY(SPEED_MOD_INST)