#pragma once
using gpio_num_t = int;
constexpr int GPIO_NUM_5=5, GPIO_NUM_6=6, GPIO_MODE_OUTPUT=1, GPIO_PULLUP_ONLY=1;
inline void gpio_set_level(int,int) {}
inline void gpio_set_direction(int,int) {}
inline void gpio_set_pull_mode(int,int) {}
