/*
 * 动态鼠标移动行为 - 支持外部实时调速
 * 基于 ZMK behavior_input_two_axis.c 修改
 */
#define DT_DRV_COMPAT zmk_behavior_dynamic_mouse_move

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/workqueue.h>
#include <math.h>
#include <dt-bindings/zmk/pointing.h>
#include <drivers/behavior.h>

#include "behavior_dynamic_mouse_move.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct dynamic_mouse_move_config {
    uint16_t x_input_code;
    uint16_t y_input_code;
    uint32_t time_to_max_speed_ms;
    uint8_t acceleration_exponent;
};

struct dynamic_mouse_move_data {
    int64_t x_target;            // 目标速度（带符号），来自 MOVE_X/MOVE_Y 参数
    int64_t y_target;
    int64_t x_start_time;       // 轴激活的起始时间
    int64_t y_start_time;
    float x_accum;              // 位移小数余量
    float y_accum;
    bool x_active;
    bool y_active;
    float speed_factor;         // 外部调速系数
    struct k_work_delayable work;
    const struct device *dev;
};

void dyn_mmv_set_speed_factor(const struct device *dev, float factor) {
    struct dynamic_mouse_move_data *data = dev->data;
    data->speed_factor = factor;
}

/* 解析 MOVE_X/MOVE_Y 参数，获取轴和增量值 */
static void decode_move_param(uint32_t param, bool *is_x, int32_t *delta) {
    // ZMK 的 MOVE_X(value) 通常将轴信息编码在高位，增量值在低位
    // 具体编码可能因版本而异，这里采用常见格式：bit 16 为 1 表示 X 轴，否则 Y 轴
    // 带符号增量在低 16 位或低 8 位，根据实际源码调整
    // 最通用的方法是调用 ZMK 提供的宏，但我们直接模拟：
    // 假设 param 是一个 int16_t 的 delta，以及一个轴标志
    // 安全起见，我们先参考官方解析方式：
    // 官方代码中：x = ZMK_POINTING_GET_X(param); y = ZMK_POINTING_GET_Y(param);
    // 你可以直接使用这些宏，如果已包含 <dt-bindings/zmk/pointing.h>
    if (ZMK_POINTING_IS_X(param)) {
        *is_x = true;
        *delta = ZMK_POINTING_GET_VALUE(param);
    } else if (ZMK_POINTING_IS_Y(param)) {
        *is_x = false;
        *delta = ZMK_POINTING_GET_VALUE(param);
    } else {
        // 默认兼容旧版：假设高 24 位为轴（0x00=X, 0x01=Y），低 8 位为有符号增量
        uint8_t axis = (param >> 24) & 0xFF;
        *delta = (int8_t)(param & 0xFF);
        *is_x = (axis == 0x00);
    }
}

/* 定时器回调：计算位移并上报 */
static void dyn_mmv_tick(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct dynamic_mouse_move_data *data =
    CONTAINER_OF(dwork, struct dynamic_mouse_move_data, work);
    const struct dynamic_mouse_move_config *cfg = data->dev->config;

    int64_t now = k_uptime_get();
    bool report_x = false, report_y = false;
    int32_t dx = 0, dy = 0;

    /* 处理 X 轴 */
    if (data->x_active) {
        int64_t elapsed = now - data->x_start_time;
        float t = (float)elapsed / cfg->time_to_max_speed_ms;
        if (t > 1.0f) t = 1.0f;
        float accel = (cfg->acceleration_exponent == 0) ? 1.0f :
                      powf(t, cfg->acceleration_exponent);

        // 本次时间间隔（ms）
        int64_t dt = (data->x_start_time > 0) ? (now - data->x_start_time) : 10;
        // 转换为秒
        float dt_sec = dt / 1000.0f;

        // 计算本次产生的位移 = 目标速度 * 加速度 * 时间 * 外部调速因子
        float move = (float)data->x_target * accel * dt_sec * data->speed_factor;
        // 加上之前累积的小数余量
        move += data->x_accum;
        dx = (int32_t)move;
        data->x_accum = move - dx;  // 保存小数部分
        if (dx != 0) report_x = true;
        // 更新起始时间为当前，以便下次计算增量
        data->x_start_time = now;
    }

    /* 处理 Y 轴 */
    if (data->y_active) {
        int64_t elapsed = now - data->y_start_time;
        float t = (float)elapsed / cfg->time_to_max_speed_ms;
        if (t > 1.0f) t = 1.0f;
        float accel = (cfg->acceleration_exponent == 0) ? 1.0f :
                      powf(t, cfg->acceleration_exponent);
        int64_t dt = (data->y_start_time > 0) ? (now - data->y_start_time) : 10;
        float dt_sec = dt / 1000.0f;

        float move = (float)data->y_target * accel * dt_sec * data->speed_factor;
        move += data->y_accum;
        dy = (int32_t)move;
        data->y_accum = move - dy;
        if (dy != 0) report_y = true;
        data->y_start_time = now;
    }

    if (report_x) {
        input_report_rel(data->dev, cfg->x_input_code, dx, false, K_NO_WAIT);
    }
    if (report_y) {
        input_report_rel(data->dev, cfg->y_input_code, dy, false, K_NO_WAIT);
    }

    // 如果仍有轴活跃，继续调度
    if (data->x_active || data->y_active) {
        k_work_reschedule(&data->work, K_MSEC(5));
    }
}

/* 按键按下：设置目标速度并激活轴 */
static int on_dyn_mmv_binding_pressed(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct dynamic_mouse_move_data *data = dev->data;
    bool is_x;
    int32_t delta;
    decode_move_param(binding->param1, &is_x, &delta);

    if (is_x) {
        data->x_target = delta;
        data->x_active = true;
        data->x_start_time = k_uptime_get();
    } else {
        data->y_target = delta;
        data->y_active = true;
        data->y_start_time = k_uptime_get();
    }

    // 启动或重置定时器
    k_work_reschedule(&data->work, K_MSEC(5));
    return 0;
}

/* 按键释放：停用轴 */
static int on_dyn_mmv_binding_released(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct dynamic_mouse_move_data *data = dev->data;
    bool is_x;
    int32_t delta;
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

static const struct behavior_driver_api dyn_mmv_api = {
        .binding_pressed = on_dyn_mmv_binding_pressed,
        .binding_released = on_dyn_mmv_binding_released,
};

static int dyn_mmv_init(const struct device *dev) {
    struct dynamic_mouse_move_data *data = dev->data;
    data->speed_factor = 1.0f;
    k_work_init_delayable(&data->work, dyn_mmv_tick);
    data->dev = dev;
    return 0;
}

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