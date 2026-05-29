#define DT_DRV_COMPAT zmk_behavior_kp2

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

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

static const struct behavior_driver_api kp2_driver_api = {
        .binding_pressed = on_kp2_pressed,
        .binding_released = on_kp2_released,
};

#define KP2_INST(n) \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL, \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &kp2_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KP2_INST)