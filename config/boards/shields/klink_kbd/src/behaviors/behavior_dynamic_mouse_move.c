#define DT_DRV_COMPAT zmk_behavior_dynamic_mouse_move

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zmk/behavior.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// ===== 基础工具（与 behavior_input_two_axis.c 相同） =====
struct vector2d { float x; float y; };
struct movement_state_1d { float remainder; int16_t speed; int64_t start_time; };
struct movement_state_2d { struct movement_state_1d x; struct movement_state_1d y; };

struct behavior_dynamic_mouse_move_config {
    int16_t x_code;
    int16_t y_code;
    uint16_t delay_ms;
    uint16_t time_to_max_speed_ms;
    uint8_t trigger_period_ms;
    uint8_t acceleration_exponent;
};

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
    int64_t d = now - (start + delay);
    return d < 0 ? 0 : d;
}

static float speed(const struct behavior_dynamic_mouse_move_config *cfg, float max_speed,
                   int64_t duration_ticks) {
    if (cfg->time_to_max_speed_ms == 0 || cfg->acceleration_exponent == 0 ||
        (1000 * duration_ticks / CONFIG_SYS_CLOCK_TICKS_PER_SEC) > cfg->time_to_max_speed_ms)
        return max_speed;
    if (duration_ticks == 0) return 0;
    float frac = (float)(1000 * duration_ticks / CONFIG_SYS_CLOCK_TICKS_PER_SEC) /
                 cfg->time_to_max_speed_ms;
    return max_speed * powf(frac, cfg->acceleration_exponent);
}

static void track_remainder(float *move, float *remainder) {
    float new_move = *move + *remainder;
    *remainder = new_move - (int)new_move;
    *move = (int)new_move;
}

static float update_movement_1d(const struct behavior_dynamic_mouse_move_config *cfg,
                                struct movement_state_1d *st, int64_t now) {
    if (st->speed == 0) { st->remainder = 0; return 0; }
    int64_t dur = ticks_since_start(st->start_time, now, cfg->delay_ms);
    float move = (dur > 0) ? speed(cfg, st->speed, dur) * cfg->trigger_period_ms / 1000 : 0;
    track_remainder(&move, &st->remainder);
    return move;
}

static struct vector2d update_movement_2d(const struct behavior_dynamic_mouse_move_config *cfg,
                                          struct movement_state_2d *st, int64_t now) {
    return (struct vector2d){
            .x = update_movement_1d(cfg, &st->x, now),
            .y = update_movement_1d(cfg, &st->y, now),
    };
}

static bool is_non_zero_2d(struct movement_state_2d *st) {
    return st->x.speed != 0 || st->y.speed != 0;
}

// ===== 双速状态（极简设计） =====
struct behavior_dynamic_mouse_move_data {
    struct k_work_delayable tick_work;
    const struct device *dev;
    struct movement_state_2d state;
    int16_t quick_sum_x, quick_sum_y;   // 所有按下键的 param1 贡献总和
    int16_t slow_sum_x, slow_sum_y;     // 所有按下键的 param2 贡献总和
    uint8_t active_slot;                // 0：快速槽，1：慢速槽
};

static void adjust_speed(const struct device *dev, int16_t dx, int16_t dy) {
    struct behavior_dynamic_mouse_move_data *data = dev->data;
    data->state.x.speed += dx;
    data->state.y.speed += dy;
}

static void set_start_times(struct movement_state_2d *st) {
    if (st->x.speed != 0 && st->x.start_time == 0) st->x.start_time = k_uptime_ticks();
    else if (st->x.speed == 0) st->x.start_time = 0;
    if (st->y.speed != 0 && st->y.start_time == 0) st->y.start_time = k_uptime_ticks();
    else if (st->y.speed == 0) st->y.start_time = 0;
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

static void tick_work_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct behavior_dynamic_mouse_move_data *data =
    CONTAINER_OF(dwork, struct behavior_dynamic_mouse_move_data, tick_work);
    const struct device *dev = data->dev;
    const struct behavior_dynamic_mouse_move_config *cfg = dev->config;

    struct vector2d move = update_movement_2d(cfg, &data->state, k_uptime_ticks());
    if (move.x) input_report_rel(dev, cfg->x_code, CLAMP(move.x, INT16_MIN, INT16_MAX), !move.y, K_NO_WAIT);
    if (move.y) input_report_rel(dev, cfg->y_code, CLAMP(move.y, INT16_MIN, INT16_MAX), true, K_NO_WAIT);

    if (is_non_zero_2d(&data->state))
        k_work_schedule(&data->tick_work, K_MSEC(cfg->trigger_period_ms));
}

// 供 speed_change 调用的接口
void behavior_dmmv_set_active_slot(const struct device *dev, uint8_t slot) {
    struct behavior_dynamic_mouse_move_data *data = dev->data;
    if (data->active_slot == slot) return;

    int16_t old_x = (data->active_slot == 0) ? data->quick_sum_x : data->slow_sum_x;
    int16_t old_y = (data->active_slot == 0) ? data->quick_sum_y : data->slow_sum_y;
    int16_t new_x = (slot == 0) ? data->quick_sum_x : data->slow_sum_x;
    int16_t new_y = (slot == 0) ? data->quick_sum_y : data->slow_sum_y;

    data->active_slot = slot;
    adjust_speed(dev, new_x - old_x, new_y - old_y);
    update_work_scheduling(dev);
}

// 按键处理
static inline int16_t param_x(uint32_t p) { return (int16_t)(p >> 16); }
static inline int16_t param_y(uint32_t p) { return (int16_t)(p & 0xFFFF); }

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_dynamic_mouse_move_data *data = dev->data;

    int16_t x1 = param_x(binding->param1), y1 = param_y(binding->param1);
    int16_t x2 = param_x(binding->param2), y2 = param_y(binding->param2);

    // 累加总贡献
    data->quick_sum_x += x1; data->quick_sum_y += y1;
    data->slow_sum_x  += x2; data->slow_sum_y  += y2;

    // 应用当前槽的速度增量
    int16_t cx = (data->active_slot == 0) ? x1 : x2;
    int16_t cy = (data->active_slot == 0) ? y1 : y2;
    adjust_speed(dev, cx, cy);
    update_work_scheduling(dev);
    return 0;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_dynamic_mouse_move_data *data = dev->data;

    int16_t x1 = param_x(binding->param1), y1 = param_y(binding->param1);
    int16_t x2 = param_x(binding->param2), y2 = param_y(binding->param2);

    // 从总和扣除
    data->quick_sum_x -= x1; data->quick_sum_y -= y1;
    data->slow_sum_x  -= x2; data->slow_sum_y  -= y2;

    // 减去当前槽的速度贡献
    int16_t cx = (data->active_slot == 0) ? x1 : x2;
    int16_t cy = (data->active_slot == 0) ? y1 : y2;
    adjust_speed(dev, -cx, -cy);
    update_work_scheduling(dev);
    return 0;
}

static int behavior_dynamic_mouse_move_init(const struct device *dev) {
    struct behavior_dynamic_mouse_move_data *data = dev->data;
    data->dev = dev;
    data->active_slot = 0;
    data->quick_sum_x = data->quick_sum_y = 0;
    data->slow_sum_x  = data->slow_sum_y  = 0;
    k_work_init_delayable(&data->tick_work, tick_work_cb);
    return 0;
}

static const struct behavior_driver_api api = {
        .binding_pressed = on_keymap_binding_pressed,
        .binding_released = on_keymap_binding_released,
};

#define DMMV_INST(n)                                                                               \
    static struct behavior_dynamic_mouse_move_data dmmv_data_##n;                                  \
    static const struct behavior_dynamic_mouse_move_config dmmv_config_##n = {                     \
        .x_code = DT_INST_PROP(n, x_input_code),                                                   \
        .y_code = DT_INST_PROP(n, y_input_code),                                                   \
        .trigger_period_ms = DT_INST_PROP(n, trigger_period_ms),                                   \
        .delay_ms = DT_INST_PROP_OR(n, delay_ms, 0),                                               \
        .time_to_max_speed_ms = DT_INST_PROP(n, time_to_max_speed_ms),                             \
        .acceleration_exponent = DT_INST_PROP_OR(n, acceleration_exponent, 1),                     \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_dynamic_mouse_move_init, NULL, &dmmv_data_##n,            \
                            &dmmv_config_##n, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,    \
                            &api);

DT_INST_FOREACH_STATUS_OKAY(DMMV_INST)