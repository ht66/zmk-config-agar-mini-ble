# Copyright (c) 2025 YANG
# zmk klink indicator
zephyr_library()
zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
zephyr_library_sources(src/klink_indicator.c)

zephyr_library_sources(src/behaviors/kp2.c src/behaviors/toggle.c src/behaviors/mv.c src/behaviors/behavior_dynamic_mouse_move.c src/behaviors/behavior_speed_change.c)