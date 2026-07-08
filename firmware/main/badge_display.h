#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BADGE_ID_LED_COUNT 6

typedef enum {
    BADGE_DISPLAY_GROUP_MY_ID = 0,
    BADGE_DISPLAY_GROUP_SEND_ID,
} badge_display_group_t;

esp_err_t badge_display_init(void);
void badge_display_set_my_id(uint8_t id, bool blink);
void badge_display_set_target_id(uint8_t id, bool blink);
void badge_display_set_send_led(bool enabled);
void badge_display_set_receive_led(bool enabled);
void badge_display_set_send_led_raw(bool enabled, bool level);
void badge_display_set_receive_led_raw(bool enabled, bool level);
void badge_display_pulse_send(void);
void badge_display_pulse_receive(void);
void badge_display_play_busy_dance(void);
void badge_display_play_error_dance(void);
void badge_display_tick(void);
void badge_display_set_order(badge_display_group_t group,
                             const uint8_t order[BADGE_ID_LED_COUNT]);

#ifdef __cplusplus
}
#endif
