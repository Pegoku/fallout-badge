#pragma once

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpio_num_t my_id[3];
    gpio_num_t send_id[3];
    gpio_num_t send_led;
    gpio_num_t receive_led;
    gpio_num_t button_up;
    gpio_num_t button_down;
    gpio_num_t button_action;
} badge_pin_map_t;

extern const badge_pin_map_t BADGE_PINS;

#ifdef __cplusplus
}
#endif
