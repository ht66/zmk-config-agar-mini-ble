#define DT_DRV_COMPAT zmk_behavior_dynamic_mouse_move

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/behavior.h>
#include <dt-bindings/zmk/pointing.h>

#if IS_ENABLED(CONFIG_ZMK_POINTING_SMOOTH_SCROLLING)
#include <zmk/pointing/resolution_multipliers.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// ---- 复用原来的二维移动状态和工具函数（直接拷贝自 behavior_input_two_axis.c） ----
struct vector2d { float x; float y; };
struct movement_state_1d { float remainder; int16_t speed; int64_t start_time; };
struct movement_state_2d { struct movement_state_1d x; struct movement_state_1d y; };

#if CONFIG_MINIMAL_LIBC
static float powf(float base, float exponent) {
    float power = 1.0f;
    for (; exponent >= 1.0f; exponent--) power = power * base;
    return power;
}
#else
#include <math.h>
#endif

static int64_t ticks_since_start(int64_t start, int64_t now, int64_t delay) {
    if (start == 0) return 0;
    int64_t move_duration = now - (start + delay);
    return (move_duration < 0) ? 0 : move_duration;
}

static inline uint8_t get_acceleration_exponent(const struct behavior_dynamic_mouse_move_config *config,
                                                uint16_t code) {
    return config->acceleration_exponent;
}

static float speed(const struct behavior_dynamic_mouse_move_config *config, uint16_t code,
                   float max_speed, int64_t duration_ticks) {
    uint8_t accel_exp = get_acceleration_exponent(config, code);
    if ((1000 * duration_ticks / CONFIG_SYS_CLOCK_TICKS_PER_SEC) > config->time_to_max_speed_ms ||
        config->time_to_max_speed_ms == 0 || accel_exp == 0)
        return max_speed;
    if (duration_ticks == 0) return 0;
    float time_fraction = (float)(1000 * duration_ticks / CONFIG_SYS_CLOCK_TICKS_PER_SEC) /
                          config->time_to_max_speed_ms;
    return max_speed * powf(time_fraction, accel_exp);
}

static void track_remainder(float *move, float *remainder) {
    float new_move = *move + *remainder;
    *remainder = new_move - (int)new_move;
    *move = (int)new_move;
}

static float update_movement_1d(const struct behavior_dynamic_mouse_move_config *config,
                                uint16_t code, struct movement_state_1d *state, int64_t now) {
    float move = 0;
    if (state->speed == 0) { state->remainder = 0; return move; }
    int64_t move_duration = ticks_since_start(state->start_time, now, config->delay_ms);
    move = (move_duration > 0) ? speed(config, code, state->speed, move_duration) *
                                 config->trigger_period_ms / 1000 : 0;
    track_remainder(&move, &state->remainder);
    return move;
}

static struct vector2d update_movement_2d(const struct behavior_dynamic_mouse_move_config *config,
                                          struct movement_state_2d *state, int64_t now) {
    struct vector2d move = {
            .x = update_movement_1d(config, config->x_code, &state->x, now),
            .y = update_movement_1d(config, config->y_code, &state->y, now),
    };
    return move;
}

static bool is_non_zero_1d(int16_t speed) { return speed != 0; }
static bool is_non_zero_2d(struct movement_state_2d *s) {
    return is_non_zero_1d(s->x.speed) || is_non_zero_1d(s->y.speed);
}
// ----------------------------------------------------------------

// 驱动自定义数据结构
struct behavior_dynamic_mouse_move_data {
    struct k_work_delayable tick_work;
    const struct device *dev;
    struct movement_state_2d state;
    uint8_t active_speed; // 1 或 2
};

struct behavior_dynamic_mouse_move_config {
    int16_t x_code;
    int16_t y_code;
    uint16_t delay_ms;
    uint16_t time_to_max_speed_ms;
    uint8_t trigger_period_ms;
    uint8_t acceleration_exponent;
    int16_t speed1;
    int16_t speed2;
};

static void adjust_speed(const struct device *dev, int16_t dx, int16_t dy) {
    struct behavior_dynamic_mouse_move_data *data = dev->data;
    data->state.x.speed += dx;
    data->state.y.speed += dy;
}

static void set_start_times_1d(struct movement_state_1d *s) {
    if (s->speed != 0 && s->start_time == 0) s->start_time = k_uptime_ticks();
    else if (s->speed == 0) s->start_time = 0;
}
static void set_start_times(struct movement_state_2d *s) {
    set_start_times_1d(&s->x);
    set_start_times_1d(&s->y);
}

static void update_work_scheduling(const struct device *dev) {
    struct behavior_dynamic_mouse_move_data *data = dev->data;
    const struct behavior_dynamic_mouse_move_config *cfg = dev->config;
    set_start_times(&data->state);
    if (is_non_zero_2d(&data->state))
        k_work_schedule(&data->tick_work, K_MSEC(cfg->trigger_period_ms));
    else {
        k_work_cancel_delayable(&data->tick_work);
        data->state.x.remainder = data->state.y.remainder = 0;
    }
}

// 公开的速度切换接口
void behavior_dmmv_set_active_speed(const struct device *dev, uint8_t speed_idx) {
    struct behavior_dynamic_mouse_move_data *data = dev->data;
    const struct behavior_dynamic_mouse_move_config *cfg = dev->config;
    if (data->active_speed == speed_idx) return;

    int16_t old_val = (data->active_speed == 1) ? cfg->speed1 : cfg->speed2;
    int16_t new_val = (speed_idx == 1) ? cfg->speed1 : cfg->speed2;

    // 无缝切换：只改变当前移动轴的速度值，保持方向不变
    if (data->state.x.speed != 0) {
        int16_t dir = (data->state.x.speed > 0) ? 1 : -1;
        int16_t delta = dir * new_val - data->state.x.speed;
        adjust_speed(dev, delta, 0);
    }
    if (data->state.y.speed != 0) {
        int16_t dir = (data->state.y.speed > 0) ? 1 : -1;
        int16_t delta = dir * new_val - data->state.y.speed;
        adjust_speed(dev, 0, delta);
    }
    data->active_speed = speed_idx;
}

// 定时器回调
static void tick_work_cb(struct k_work *work) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(work);
    struct behavior_dynamic_mouse_move_data *data =
    CONTAINER_OF(d_work, struct behavior_dynamic_mouse_move_data, tick_work);
    const struct device *dev = data->dev;
    const struct behavior_dynamic_mouse_move_config *cfg = dev->config;

    struct vector2d move = update_movement_2d(cfg, &data->state, k_uptime_ticks());
    if (move.x) input_report_rel(dev, cfg->x_code, CLAMP(move.x, INT16_MIN, INT16_MAX), !move.y, K_NO_WAIT);
    if (move.y) input_report_rel(dev, cfg->y_code, CLAMP(move.y, INT16_MIN, INT16_MAX), true, K_NO_WAIT);

    if (is_non_zero_2d(&data->state))
        k_work_schedule(&data->tick_work, K_MSEC(cfg->trigger_period_ms));
}

// 按下/释放方向键
static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_dynamic_mouse_move_data *data = dev->data;
    const struct behavior_dynamic_mouse_move_config *cfg = dev->config;

    // 从 param1 取出方向（沿用 MOVE 宏的 16+16 编码，但只关心符号）
    int16_t dir_x = (int16_t)(binding->param1 >> 16);
    int16_t dir_y = (int16_t)(binding->param1 & 0xFFFF);

    int16_t speed_val = (data->active_speed == 1) ? cfg->speed1 : cfg->speed2;
    int16_t dx = (dir_x > 0) ? speed_val : (dir_x < 0) ? -speed_val : 0;
    int16_t dy = (dir_y > 0) ? speed_val : (dir_y < 0) ? -speed_val : 0;

    adjust_speed(dev, dx, dy);
    update_work_scheduling(dev);
    return 0;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_dynamic_mouse_move_data *data = dev->data;
    const struct behavior_dynamic_mouse_move_config *cfg = dev->config;

    int16_t dir_x = (int16_t)(binding->param1 >> 16);
    int16_t dir_y = (int16_t)(binding->param1 & 0xFFFF);

    int16_t speed_val = (data->active_speed == 1) ? cfg->speed1 : cfg->speed2;
    int16_t dx = (dir_x > 0) ? -speed_val : (dir_x < 0) ? speed_val : 0;
    int16_t dy = (dir_y > 0) ? -speed_val : (dir_y < 0) ? speed_val : 0;

    adjust_speed(dev, dx, dy);
    update_work_scheduling(dev);
    return 0;
}

static int behavior_dynamic_mouse_move_init(const struct device *dev) {
    struct behavior_dynamic_mouse_move_data *data = dev->data;
    data->dev = dev;
    data->active_speed = 1; // 默认慢速
    k_work_init_delayable(&data->tick_work, tick_work_cb);
    return 0;
}

static const struct behavior_driver_api dynamic_mouse_move_api = {
        .binding_pressed = on_keymap_binding_pressed,
        .binding_released = on_keymap_binding_released,
};

#define DMMV_INST(n)                                                                               \
    static struct behavior_dynamic_mouse_move_data dmmv_data_##n = { .active_speed = 1 };         \
    static const struct behavior_dynamic_mouse_move_config dmmv_config_##n = {                     \
        .x_code = DT_INST_PROP(n, x_input_code),                                                   \
        .y_code = DT_INST_PROP(n, y_input_code),                                                   \
        .trigger_period_ms = DT_INST_PROP(n, trigger_period_ms),                                   \
        .delay_ms = DT_INST_PROP_OR(n, delay_ms, 0),                                               \
        .time_to_max_speed_ms = DT_INST_PROP(n, time_to_max_speed_ms),                             \
        .acceleration_exponent = DT_INST_PROP_OR(n, acceleration_exponent, 1),                     \
        .speed1 = DT_INST_PROP(n, speed1),                                                         \
        .speed2 = DT_INST_PROP(n, speed2),                                                         \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_dynamic_mouse_move_init, NULL, &dmmv_data_##n,            \
                            &dmmv_config_##n, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,    \
                            &dynamic_mouse_move_api);

DT_INST_FOREACH_STATUS_OKAY(DMMV_INST)