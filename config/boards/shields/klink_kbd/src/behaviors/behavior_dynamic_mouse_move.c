/*
 * 基于 ZMK behavior_input_two_axis.c 修改
 * 增加实时速度因子控制，支持外部动态调速
 */
#define DT_DRV_COMPAT zmk_behavior_dynamic_mouse_move

#include <zephyr/device.h>
#include <zephyr/drivers/behavior.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/workqueue.h>
#include <math.h>

#include "behavior_dynamic_mouse_move.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* 配置：来自设备树 */
struct dynamic_mouse_move_config {
    uint16_t x_input_code;
    uint16_t y_input_code;
    uint32_t time_to_max_speed_ms;
    uint8_t acceleration_exponent;
};

/* 运行时数据 */
struct dynamic_mouse_move_data {
    int64_t x_accum;
    int64_t y_accum;
    int64_t last_update;
    bool x_active;
    bool y_active;
    float speed_factor;          /* 可动态调整 */
    struct k_work_delayable work;
    const struct device *dev;
};

/* 供外部调用的接口：修改速度因子 */
void dyn_mmv_set_speed_factor(const struct device *dev, float factor) {
    struct dynamic_mouse_move_data *data = dev->data;
    data->speed_factor = factor;
}

/* 定时器回调：计算位移并上报 */
static void dyn_mmv_tick(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct dynamic_mouse_move_data *data =
    CONTAINER_OF(dwork, struct dynamic_mouse_move_data, work);
    const struct dynamic_mouse_move_config *cfg = data->dev->config;

    int64_t now = k_uptime_get();
    int64_t dt = now - data->last_update;
    data->last_update = now;

    /* 加速度曲线计算 */
    float t = (float)dt / cfg->time_to_max_speed_ms;
    if (t > 1.0f) t = 1.0f;
    float accel = (cfg->acceleration_exponent == 0) ?
                  1.0f : powf(t, cfg->acceleration_exponent);

    /* 最终速度因子 = 外部系数 × 加速度 */
    float factor = data->speed_factor * accel;
    int32_t dx = (int32_t)((float)data->x_accum * factor);
    int32_t dy = (int32_t)((float)data->y_accum * factor);

    if (dx) {
        input_report_rel(data->dev, cfg->x_input_code, dx, false, K_FOREVER);
    }
    if (dy) {
        input_report_rel(data->dev, cfg->y_input_code, dy, false, K_FOREVER);
    }

    /* 上报后清零累积量（注意：此处简化，原版会保留余数） */
    data->x_accum = 0;
    data->y_accum = 0;

    if (data->x_active || data->y_active) {
        k_work_reschedule(&data->work, K_MSEC(10));
    }
}

/* 解析按键绑定参数，兼容官方 MOVE_X/MOVE_Y 宏 */
/* 官方宏定义通常将轴和增量打包在 32 位参数中，这里采用常见格式：
   - 字节3：轴类型 (0x00=X, 0x01=Y)
   - 字节0：有符号增量 (int8_t)
   请根据实际 ZMK 版本调整！
*/
static void decode_move_param(uint32_t param, bool *is_x, int8_t *delta) {
    uint8_t axis_type = (param >> 24) & 0xFF;
    *delta = (int8_t)(param & 0xFF);
    *is_x = (axis_type == 0x00);
}

/* 按键按下：解析轴和增量，更新累加器 */
static int on_dyn_mmv_binding_pressed(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_device(binding->behavior_dev);
    struct dynamic_mouse_move_data *data = dev->data;
    bool is_x;
    int8_t delta;

    decode_move_param(binding->param1, &is_x, &delta);

    if (is_x) {
        data->x_accum += delta;
        data->x_active = true;
    } else {
        data->y_accum += delta;
        data->y_active = true;
    }

    /* 启动/重置定时器 */
    data->last_update = k_uptime_get();
    k_work_reschedule(&data->work, K_MSEC(10));
    return 0;
}

/* 按键释放：关闭对应轴 */
static int on_dyn_mmv_binding_released(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_device(binding->behavior_dev);
    struct dynamic_mouse_move_data *data = dev->data;
    bool is_x;
    int8_t delta;
    decode_move_param(binding->param1, &is_x, &delta);

    if (is_x) {
        data->x_active = false;
    } else {
        data->y_active = false;
    }

    if (!data->x_active && !data->y_active) {
        k_work_cancel_delayable(&data->work);
    }
    return 0;
}

/* 行为驱动 API 结构体 */
static const struct behavior_driver_api dyn_mmv_api = {
        .binding_pressed = on_dyn_mmv_binding_pressed,
        .binding_released = on_dyn_mmv_binding_released,
};

/* 初始化 */
static int dyn_mmv_init(const struct device *dev) {
    struct dynamic_mouse_move_data *data = dev->data;
    data->speed_factor = 1.0f;
    k_work_init_delayable(&data->work, dyn_mmv_tick);
    data->dev = dev;
    return 0;
}

/* 设备树实例化宏 */
#define DYN_MMV_INST(n)                                                        \
    static const struct dynamic_mouse_move_config dyn_mmv_cfg_##n = {          \
        .x_input_code = DT_INST_PROP(n, x_input_code),                         \
        .y_input_code = DT_INST_PROP(n, y_input_code),                         \
        .time_to_max_speed_ms = DT_INST_PROP(n, time_to_max_speed_ms),         \
        .acceleration_exponent = DT_INST_PROP(n, acceleration_exponent),       \
    };                                                                         \
    static struct dynamic_mouse_move_data dyn_mmv_data_##n;                    \
    DEVICE_DT_INST_DEFINE(n, dyn_mmv_init, NULL,                               \
                          &dyn_mmv_data_##n, &dyn_mmv_cfg_##n,                 \
                          APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,    \
                          &dyn_mmv_api);

DT_INST_FOREACH_STATUS_OKAY(DYN_MMV_INST)