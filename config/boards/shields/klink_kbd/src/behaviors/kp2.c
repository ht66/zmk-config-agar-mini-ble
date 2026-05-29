#include <zephyr/kernel.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keymap.h>

// 由 toggle.c 维护的全局开关
extern bool kp2_alt_active;

// 每个物理按键位置的状态（按 keymap 索引存储）
struct kp2_state {
    bool active;
    uint16_t original_code;
    uint16_t alt_code;
    uint16_t last_sent_code;
};

// 如果 ZMK_KEYMAP_LEN 未定义，则使用一个安全值
#ifndef ZMK_KEYMAP_LEN
#define ZMK_KEYMAP_LEN 128
#endif

static struct kp2_state states[ZMK_KEYMAP_LEN];

/**
 * @brief 根据当前开关状态更新该位置的实际输出
 */
static void kp2_update(struct kp2_state *state, uint32_t timestamp) {
    uint16_t want = kp2_alt_active ? state->alt_code : state->original_code;
    if (want != state->last_sent_code) {
        if (state->last_sent_code != 0) {
            raise_zmk_keycode_state_changed_from_encoded(state->last_sent_code, false, timestamp);
        }
        raise_zmk_keycode_state_changed_from_encoded(want, true, timestamp);
        state->last_sent_code = want;
    }
}

/**
 * @brief 由 toggle 调用：刷新所有正在按下的 &kp2 输出
 */
void kp2_refresh_all(uint32_t timestamp) {
    for (int i = 0; i < ZMK_KEYMAP_LEN; i++) {
        if (states[i].active) {
            kp2_update(&states[i], timestamp);
        }
    }
}

// 按下处理
static int on_kp2_pressed(struct zmk_behavior_binding *binding,
                          struct zmk_behavior_binding_event event) {
    uint32_t pos = event.position;
    if (pos >= ZMK_KEYMAP_LEN) return -EINVAL;

    struct kp2_state *state = &states[pos];
    state->active = true;
    state->original_code = binding->param1;
    state->alt_code = binding->param2;
    state->last_sent_code = 0;

    kp2_update(state, event.timestamp);
    return ZMK_BEHAVIOR_OPAQUE;
}

// 释放处理
static int on_kp2_released(struct zmk_behavior_binding *binding,
                           struct zmk_behavior_binding_event event) {
    uint32_t pos = event.position;
    if (pos >= ZMK_KEYMAP_LEN) return -EINVAL;

    struct kp2_state *state = &states[pos];
    state->active = false;

    if (state->last_sent_code != 0) {
        raise_zmk_keycode_state_changed_from_encoded(state->last_sent_code, false,
                                                     event.timestamp);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

// 行为驱动 API（不再使用 slice）
static const struct behavior_driver_api kp2_driver_api = {
        .binding_pressed = on_kp2_pressed,
        .binding_released = on_kp2_released,
        .data_size = 0,   // 我们自己管理状态，不需要框架额外分配
};

ZMK_BEHAVIOR_DEFINE(kp2, kp2_driver_api);