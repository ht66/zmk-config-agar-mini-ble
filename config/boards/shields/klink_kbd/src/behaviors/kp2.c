#include <zephyr/kernel.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

// 由 toggle.c 维护的全局开关
extern bool kp2_alt_active;

// 最多允许同时按下的 &kp2 实例数（可自行调整）
#define MAX_KP2_INSTANCES 16

struct kp2_state {
    bool active;                 // 物理按键是否被按住
    uint16_t original_code;      // 参数1：原始键码
    uint16_t alt_code;           // 参数2：备用键码
    uint16_t last_sent_code;     // 当前正发送给主机的键码（用于切换时释放）
    struct kp2_state *next;      // 链表指针
};

static struct kp2_state *kp2_active_head = NULL;
static struct kp2_state kp2_states[MAX_KP2_INSTANCES];
static int kp2_state_count = 0;

/**
 * @brief 根据当前开关状态，更新该实例发送的键码
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
 * @brief 由 toggle 调用的接口：通知所有活跃的 &kp2 刷新输出
 */
void kp2_refresh_all(uint32_t timestamp) {
    struct kp2_state *p = kp2_active_head;
    while (p) {
        if (p->active) {
            kp2_update(p, timestamp);
        }
        p = p->next;
    }
}

static int on_kp2_pressed(struct zmk_behavior_binding *binding,
                          struct zmk_behavior_binding_event event) {
    if (kp2_state_count >= MAX_KP2_INSTANCES) return -ENOMEM;

    struct kp2_state *state = &kp2_states[kp2_state_count++];
    state->active = true;
    state->original_code = binding->param1;
    state->alt_code = binding->param2;
    state->last_sent_code = 0;

    // 插入链表头
    state->next = kp2_active_head;
    kp2_active_head = state;

    // 发送初始按键
    kp2_update(state, event.timestamp);

    // 将实例指针保存到绑定点，释放时取出
    zmk_behavior_set_binding_data(binding, state);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_kp2_released(struct zmk_behavior_binding *binding,
                           struct zmk_behavior_binding_event event) {
    struct kp2_state *state = zmk_behavior_get_binding_data(binding);
    if (!state) return -ENODEV;

    state->active = false;

    // 释放当前按键
    if (state->last_sent_code != 0) {
        raise_zmk_keycode_state_changed_from_encoded(state->last_sent_code, false,
                                                     event.timestamp);
    }

    // 从链表中删除该节点
    struct kp2_state **indirect = &kp2_active_head;
    while (*indirect) {
        if (*indirect == state) {
            *indirect = state->next;
            break;
        }
        indirect = &(*indirect)->next;
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct zmk_behavior_behavior_slice kp2_slices[] = {
        ZMK_BEHAVIOR_SLICE(ON_PRESS, on_kp2_pressed),
        ZMK_BEHAVIOR_SLICE(ON_RELEASE, on_kp2_released),
};

static const struct zmk_behavior_behavior kp2_behavior = {
        .name = "KP2",
        .slices = kp2_slices,
        .slices_count = ARRAY_SIZE(kp2_slices),
        // 注意：这里存的是指针（指向静态数组中的结构体），而不是结构体本身
        .data_size = sizeof(struct kp2_state *),
};

ZMK_BEHAVIOR_DEFINE(kp2, kp2_behavior);