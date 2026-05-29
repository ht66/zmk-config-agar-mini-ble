#define DT_DRV_COMPAT zmk_behavior_mv

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// 速度开关标志（由 toggle 维护）
extern bool kp2_alt_active;

// 获取我们在 overlay 中定义的 mva 设备指针
#define MV_DEV DEVICE_DT_GET(DT_NODELABEL(mva))

// 声明 behavior_input_two_axis 提供的调节函数
extern int behavior_input_two_axis_adjust_speed(const struct device *dev, int16_t dx, int16_t dy);

// 方向枚举（与 keymap 宏对应）
enum mv_dir {
    MV_UP    = 0,
    MV_DOWN  = 1,
    MV_LEFT  = 2,
    MV_RIGHT = 3,
};

// 从绑定的参数中解析方向和速度
static void mv_get_params(struct zmk_behavior_binding *binding, uint8_t *dir,
                          int8_t *fast, int8_t *slow) {
    *dir = (binding->param1 >> 8) & 0xFF;
    *fast = (int8_t)(binding->param1 & 0xFF);
    *slow = (int8_t)(binding->param2 & 0xFF);
}

static int on_mv_pressed(struct zmk_behavior_binding *binding,
                         struct zmk_behavior_binding_event event) {
    uint8_t dir;
    int8_t fast, slow;
    mv_get_params(binding, &dir, &fast, &slow);

    // 根据当前开关状态选择速度值
    int8_t speed = kp2_alt_active ? slow : fast;
    int16_t dx = 0, dy = 0;

    switch (dir) {
        case MV_UP:    dy = -speed; break;
        case MV_DOWN:  dy =  speed; break;
        case MV_LEFT:  dx = -speed; break;
        case MV_RIGHT: dx =  speed; break;
    }

    // 调用引擎的 adjust_speed，增加移动速度
    behavior_input_two_axis_adjust_speed(MV_DEV, dx, dy);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_mv_released(struct zmk_behavior_binding *binding,
                          struct zmk_behavior_binding_event event) {
    uint8_t dir;
    int8_t fast, slow;
    mv_get_params(binding, &dir, &fast, &slow);

    int8_t speed = kp2_alt_active ? slow : fast;
    int16_t dx = 0, dy = 0;

    switch (dir) {
        case MV_UP:    dy = -speed; break;
        case MV_DOWN:  dy =  speed; break;
        case MV_LEFT:  dx = -speed; break;
        case MV_RIGHT: dx =  speed; break;
    }

    // 释放时减去之前增加的速度，让引擎停止该方向的移动
    behavior_input_two_axis_adjust_speed(MV_DEV, -dx, -dy);
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