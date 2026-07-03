#include "badge_buttons.h"

#include <stdbool.h>
#include <stddef.h>

#include "badge_hardware.h"
#include "driver/gpio.h"
#include "esp_check.h"

typedef struct {
    badge_button_id_t id;
    gpio_num_t pin;
    bool stable_pressed;
    bool last_raw_pressed;
    uint32_t last_raw_change_ms;
    uint32_t press_started_ms;
    bool long_sent;
} button_state_t;

#define DEBOUNCE_MS 30U
#define LONG_PRESS_MS 800U
#define CHORD_MS 3000U
#define EVENT_QUEUE_SIZE 16U

static button_state_t s_buttons[] = {
    {.id = BADGE_BUTTON_UP},
    {.id = BADGE_BUTTON_DOWN},
    {.id = BADGE_BUTTON_ACTION},
};

static badge_button_event_t s_events[EVENT_QUEUE_SIZE];
static uint8_t s_event_head;
static uint8_t s_event_tail;
static bool s_chord_sent;

static bool queue_is_full(void)
{
    return (uint8_t)(s_event_head + 1U) % EVENT_QUEUE_SIZE == s_event_tail;
}

static bool queue_is_empty(void)
{
    return s_event_head == s_event_tail;
}

static void push_event(badge_button_id_t button, badge_button_event_type_t type,
                       uint32_t duration_ms)
{
    if (queue_is_full()) {
        return;
    }

    s_events[s_event_head] = (badge_button_event_t){
        .button = button,
        .type = type,
        .duration_ms = duration_ms,
    };
    s_event_head = (uint8_t)(s_event_head + 1U) % EVENT_QUEUE_SIZE;
}

static bool read_pressed(gpio_num_t pin)
{
    return gpio_get_level(pin) == 0;
}

esp_err_t badge_buttons_init(void)
{
    s_buttons[BADGE_BUTTON_UP].pin = BADGE_PINS.button_up;
    s_buttons[BADGE_BUTTON_DOWN].pin = BADGE_PINS.button_down;
    s_buttons[BADGE_BUTTON_ACTION].pin = BADGE_PINS.button_action;

    gpio_config_t config = {
        .pin_bit_mask = (1ULL << BADGE_PINS.button_up) |
                        (1ULL << BADGE_PINS.button_down) |
                        (1ULL << BADGE_PINS.button_action),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), "badge_buttons",
                        "button GPIO config failed");

    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
        s_buttons[i].last_raw_pressed = read_pressed(s_buttons[i].pin);
        s_buttons[i].stable_pressed = s_buttons[i].last_raw_pressed;
    }

    return ESP_OK;
}

void badge_buttons_poll(uint32_t now_ms)
{
    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
        button_state_t *button = &s_buttons[i];
        const bool raw_pressed = read_pressed(button->pin);

        if (raw_pressed != button->last_raw_pressed) {
            button->last_raw_pressed = raw_pressed;
            button->last_raw_change_ms = now_ms;
            continue;
        }

        if ((now_ms - button->last_raw_change_ms) < DEBOUNCE_MS) {
            continue;
        }

        if (raw_pressed != button->stable_pressed) {
            button->stable_pressed = raw_pressed;
            if (button->stable_pressed) {
                button->press_started_ms = now_ms;
                button->long_sent = false;
                push_event(button->id, BADGE_BUTTON_EVENT_PRESS, 0);
            } else {
                const uint32_t duration = now_ms - button->press_started_ms;
                push_event(button->id, BADGE_BUTTON_EVENT_RELEASE, duration);
                if (!button->long_sent) {
                    push_event(button->id, BADGE_BUTTON_EVENT_SHORT, duration);
                }
            }
        }

        if (button->stable_pressed && !button->long_sent &&
            (now_ms - button->press_started_ms) >= LONG_PRESS_MS) {
            button->long_sent = true;
            push_event(button->id, BADGE_BUTTON_EVENT_LONG,
                       now_ms - button->press_started_ms);
        }
    }

    const bool up_pressed = s_buttons[BADGE_BUTTON_UP].stable_pressed;
    const bool down_pressed = s_buttons[BADGE_BUTTON_DOWN].stable_pressed;

    if (up_pressed && down_pressed) {
        const uint32_t up_duration = now_ms - s_buttons[BADGE_BUTTON_UP].press_started_ms;
        const uint32_t down_duration =
            now_ms - s_buttons[BADGE_BUTTON_DOWN].press_started_ms;
        if (!s_chord_sent && up_duration >= CHORD_MS && down_duration >= CHORD_MS) {
            s_chord_sent = true;
            push_event(BADGE_BUTTON_UP, BADGE_BUTTON_EVENT_CHORD_UP_DOWN_3S,
                       up_duration < down_duration ? up_duration : down_duration);
        }
    } else {
        s_chord_sent = false;
    }
}

bool badge_buttons_pop_event(badge_button_event_t *event)
{
    if (event == NULL || queue_is_empty()) {
        return false;
    }

    *event = s_events[s_event_tail];
    s_event_tail = (uint8_t)(s_event_tail + 1U) % EVENT_QUEUE_SIZE;
    return true;
}

const char *badge_button_name(badge_button_id_t button)
{
    switch (button) {
    case BADGE_BUTTON_UP:
        return "up";
    case BADGE_BUTTON_DOWN:
        return "down";
    case BADGE_BUTTON_ACTION:
        return "action";
    default:
        return "unknown";
    }
}

const char *badge_button_event_name(badge_button_event_type_t event)
{
    switch (event) {
    case BADGE_BUTTON_EVENT_PRESS:
        return "press";
    case BADGE_BUTTON_EVENT_RELEASE:
        return "release";
    case BADGE_BUTTON_EVENT_SHORT:
        return "short";
    case BADGE_BUTTON_EVENT_LONG:
        return "long";
    case BADGE_BUTTON_EVENT_CHORD_UP_DOWN_3S:
        return "up_down_3s";
    default:
        return "unknown";
    }
}
