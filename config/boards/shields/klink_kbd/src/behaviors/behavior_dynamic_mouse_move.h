/*
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/device.h>

void behavior_dmmv_set_active_speed(const struct device *dev, uint8_t speed_idx);