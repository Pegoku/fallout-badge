#include <stdint.h>

#include "badge_buttons.h"
#include "badge_display.h"
#include "badge_hardware.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "fallout_badge";

#define DIAGNOSTIC_STEP_MS 350U
#define DIAGNOSTIC_STEP_COUNT 18U

static TickType_t delay_ticks_at_least_one(uint32_t ms)
{
    const TickType_t ticks = pdMS_TO_TICKS(ms);
    return ticks > 0 ? ticks : 1;
}

static void set_diagnostic_step(uint8_t step)
{
    badge_display_set_my_id(0, false);
    badge_display_set_target_id(0, false);
    badge_display_set_send_led(false);
    badge_display_set_receive_led(false);

    if (step < BADGE_ID_LED_COUNT) {
        ESP_LOGI(TAG, "LED test: my_id[%u]", (unsigned)step);
        badge_display_set_my_id((uint8_t)(1U << step), false);
        return;
    }

    if (step == 6) {
        ESP_LOGI(TAG, "LED test: my_id all");
        badge_display_set_my_id(0x3f, false);
        return;
    }

    if (step == 7) {
        ESP_LOGI(TAG, "LED test: blank");
        return;
    }

    if (step < 14) {
        const uint8_t led = step - 8U;
        ESP_LOGI(TAG, "LED test: call_id[%u]", (unsigned)led);
        badge_display_set_target_id((uint8_t)(1U << led), false);
        return;
    }

    if (step == 14) {
        ESP_LOGI(TAG, "LED test: call_id all");
        badge_display_set_target_id(0x3f, false);
        return;
    }

    if (step == 15) {
        ESP_LOGI(TAG, "LED test: SLED");
        badge_display_set_send_led(true);
        return;
    }

    if (step == 16) {
        ESP_LOGI(TAG, "LED test: RLED");
        badge_display_set_receive_led(true);
        return;
    }

    ESP_LOGI(TAG, "LED test: SLED + RLED");
    badge_display_set_send_led(true);
    badge_display_set_receive_led(true);
}

void app_main(void)
{
    ESP_ERROR_CHECK(badge_display_init());
    ESP_ERROR_CHECK(badge_buttons_init());

    ESP_LOGI(TAG, "Fallout badge firmware bring-up");
    ESP_LOGI(TAG, "Using pinout: %s", badge_hardware_pinout_name());
    ESP_LOGI(TAG, "Phase 2 diagnostics: all LEDs cycle; buttons log events");

    uint8_t diagnostic_step = 0;
    uint32_t last_pattern_ms = 0;
    set_diagnostic_step(diagnostic_step);

    while (true) {
        const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

        badge_buttons_poll(now_ms);

        badge_button_event_t event;
        while (badge_buttons_pop_event(&event)) {
            ESP_LOGI(TAG, "button=%s event=%s duration_ms=%lu",
                     badge_button_name(event.button),
                     badge_button_event_name(event.type),
                     (unsigned long)event.duration_ms);

            if (event.type == BADGE_BUTTON_EVENT_SHORT) {
                if (event.button == BADGE_BUTTON_ACTION) {
                    badge_display_pulse_send();
                } else {
                    badge_display_pulse_receive();
                }
            }
        }

        if ((now_ms - last_pattern_ms) >= DIAGNOSTIC_STEP_MS) {
            last_pattern_ms = now_ms;
            diagnostic_step = (uint8_t)((diagnostic_step + 1U) % DIAGNOSTIC_STEP_COUNT);
            set_diagnostic_step(diagnostic_step);
        }

        vTaskDelay(delay_ticks_at_least_one(10));
    }
}
