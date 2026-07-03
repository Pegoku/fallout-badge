#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BADGE_BUTTON_UP = 0,
    BADGE_BUTTON_DOWN,
    BADGE_BUTTON_ACTION,
} badge_button_id_t;

typedef enum {
    BADGE_BUTTON_EVENT_PRESS = 0,
    BADGE_BUTTON_EVENT_RELEASE,
    BADGE_BUTTON_EVENT_SHORT,
    BADGE_BUTTON_EVENT_LONG,
    BADGE_BUTTON_EVENT_CHORD_UP_DOWN_3S,
} badge_button_event_type_t;

typedef struct {
    badge_button_id_t button;
    badge_button_event_type_t type;
    uint32_t duration_ms;
} badge_button_event_t;

esp_err_t badge_buttons_init(void);
void badge_buttons_poll(uint32_t now_ms);
bool badge_buttons_pop_event(badge_button_event_t *event);
const char *badge_button_name(badge_button_id_t button);
const char *badge_button_event_name(badge_button_event_type_t event);

#ifdef __cplusplus
}
#endif
