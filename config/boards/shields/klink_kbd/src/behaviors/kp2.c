#include <zephyr/kernel.h>
#include <drivers/behavior.h>          // <-- 必须包含此头文件，获得完整类型
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keymap.h>

// 由 toggle.c 维护的全局开关
extern bool kp2_alt_active;

struct kp2_state {
    bool active;
    uint16_t original_code;
    uint16_t alt_code;
    uint16_t last_sent_code;
};

#ifndef ZMK_KEYMAP_LEN
#define ZMK_KEYMAP_LEN 128
#endif

static struct kp2_state states[ZMK_KEYMAP_LEN];

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

void kp2_refresh_all(uint32_t timestamp) {
    for (int i = 0; i < ZMK_KEYMAP_LEN; i++) {
        if (states[i].active) {
            kp2_update(&states[i], timestamp);
        }
    }
}

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

// 标准驱动接口，无 data_size 字段
static const struct behavior_driver_api kp2_driver_api = {
        .binding_pressed = on_kp2_pressed,
        .binding_released = on_kp2_released,
};

ZMK_BEHAVIOR_DEFINE(kp2, kp2_driver_api);