#include "badge_display.h"

#include <string.h>

#include "badge_hardware.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "badge_display";

typedef struct {
    uint8_t logical_value;
    bool blink;
    bool blink_on;
    uint8_t order[BADGE_ID_LED_COUNT];
} charlie_group_state_t;

static charlie_group_state_t s_my_id = {
    .order = {0, 1, 4, 5, 2, 3},
};

static charlie_group_state_t s_send_id = {
    .order = {0, 1, 4, 5, 2, 3},
};

static uint32_t s_tick_count;
static uint32_t s_send_pulse_ticks;
static uint32_t s_receive_pulse_ticks;
static esp_timer_handle_t s_refresh_timer;
static bool s_send_led_forced;
static bool s_receive_led_forced;

static const uint8_t CHARLIE_DRIVE[BADGE_ID_LED_COUNT][2] = {
    {0, 1},
    {1, 0},
    {0, 2},
    {2, 0},
    {1, 2},
    {2, 1},
};

static TickType_t delay_ticks_at_least_one(uint32_t ms)
{
    const TickType_t ticks = pdMS_TO_TICKS(ms);
    return ticks > 0 ? ticks : 1;
}

static void configure_output(gpio_num_t pin, int level)
{
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, level);
}

static void configure_high_z(gpio_num_t pin)
{
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_FLOATING);
}

static void all_charlie_high_z(const gpio_num_t pins[3])
{
    for (size_t i = 0; i < 3; i++) {
        configure_high_z(pins[i]);
    }
}

static void drive_charlie_led(const gpio_num_t pins[3], uint8_t physical_led)
{
    all_charlie_high_z(pins);

    const uint8_t high_pin = CHARLIE_DRIVE[physical_led][0];
    const uint8_t low_pin = CHARLIE_DRIVE[physical_led][1];

    configure_output(pins[high_pin], 1);
    configure_output(pins[low_pin], 0);
}

static void refresh_timer_callback(void *arg)
{
    (void)arg;
    badge_display_tick();
}

static void render_group(const gpio_num_t pins[3], const charlie_group_state_t *state,
                         uint8_t scan_slot)
{
    if (state->blink && !state->blink_on) {
        all_charlie_high_z(pins);
        return;
    }

    for (uint8_t logical_bit = 0; logical_bit < BADGE_ID_LED_COUNT; logical_bit++) {
        if (((state->logical_value >> logical_bit) & 0x1U) == 0) {
            continue;
        }

        if (scan_slot == logical_bit) {
            const uint8_t physical_led = state->order[logical_bit];
            if (physical_led < BADGE_ID_LED_COUNT) {
                drive_charlie_led(pins, physical_led);
                return;
            }
        }
    }

    all_charlie_high_z(pins);
}

esp_err_t badge_display_init(void)
{
    all_charlie_high_z(BADGE_PINS.my_id);
    all_charlie_high_z(BADGE_PINS.send_id);

    gpio_config_t led_config = {
        .pin_bit_mask = (1ULL << BADGE_PINS.send_led) |
                        (1ULL << BADGE_PINS.receive_led),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&led_config), TAG, "indicator GPIO config failed");

    badge_display_set_send_led(false);
    badge_display_set_receive_led(false);

    const esp_timer_create_args_t timer_args = {
        .callback = refresh_timer_callback,
        .name = "display_refresh",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_refresh_timer), TAG,
                        "display refresh timer create failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_refresh_timer, 1000), TAG,
                        "display refresh timer start failed");

    return ESP_OK;
}

void badge_display_set_my_id(uint8_t id, bool blink)
{
    s_my_id.logical_value = id & 0x3fU;
    s_my_id.blink = blink;
}

void badge_display_set_target_id(uint8_t id, bool blink)
{
    s_send_id.logical_value = id & 0x3fU;
    s_send_id.blink = blink;
}

void badge_display_set_send_led(bool enabled)
{
    s_send_led_forced = enabled;
}

void badge_display_set_receive_led(bool enabled)
{
    s_receive_led_forced = enabled;
}

void badge_display_pulse_send(void)
{
    s_send_pulse_ticks = 8;
}

void badge_display_pulse_receive(void)
{
    s_receive_pulse_ticks = 8;
}

void badge_display_play_busy_dance(void)
{
    ESP_LOGI(TAG, "busy dance requested");
    for (uint8_t i = 0; i < BADGE_ID_LED_COUNT; i++) {
        badge_display_set_target_id((uint8_t)(1U << i), false);
        for (uint8_t j = 0; j < 10; j++) {
            vTaskDelay(delay_ticks_at_least_one(10));
        }
    }
}

void badge_display_play_error_dance(void)
{
    ESP_LOGI(TAG, "error dance requested");
    for (uint8_t i = 0; i < 3; i++) {
        badge_display_set_my_id(0x3f, false);
        badge_display_set_target_id(0x3f, false);
        vTaskDelay(pdMS_TO_TICKS(120));
        badge_display_set_my_id(0, false);
        badge_display_set_target_id(0, false);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
}

void badge_display_tick(void)
{
    const uint8_t scan_slot = s_tick_count % BADGE_ID_LED_COUNT;
    s_tick_count++;

    const bool blink_phase = ((s_tick_count / 120U) % 2U) == 0;
    s_my_id.blink_on = blink_phase;
    s_send_id.blink_on = blink_phase;

    render_group(BADGE_PINS.my_id, &s_my_id, scan_slot);
    render_group(BADGE_PINS.send_id, &s_send_id, scan_slot);

    if (s_send_led_forced || s_send_pulse_ticks > 0) {
        gpio_set_level(BADGE_PINS.send_led, 1);
        if (s_send_pulse_ticks > 0) {
            s_send_pulse_ticks--;
        }
    } else {
        gpio_set_level(BADGE_PINS.send_led, 0);
    }

    if (s_receive_led_forced || s_receive_pulse_ticks > 0) {
        gpio_set_level(BADGE_PINS.receive_led, 1);
        if (s_receive_pulse_ticks > 0) {
            s_receive_pulse_ticks--;
        }
    } else {
        gpio_set_level(BADGE_PINS.receive_led, 0);
    }
}

void badge_display_set_order(badge_display_group_t group,
                             const uint8_t order[BADGE_ID_LED_COUNT])
{
    charlie_group_state_t *state =
        group == BADGE_DISPLAY_GROUP_MY_ID ? &s_my_id : &s_send_id;

    memcpy(state->order, order, BADGE_ID_LED_COUNT);
}
