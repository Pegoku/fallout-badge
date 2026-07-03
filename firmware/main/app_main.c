#include <stdint.h>

#include "badge_buttons.h"
#include "badge_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "fallout_badge";

void app_main(void)
{
    ESP_ERROR_CHECK(badge_display_init());
    ESP_ERROR_CHECK(badge_buttons_init());

    ESP_LOGI(TAG, "Fallout badge firmware bring-up");
    ESP_LOGI(TAG, "Phase 2 diagnostics: LEDs scan; buttons log events");

    uint8_t test_value = 1;
    uint32_t last_pattern_ms = 0;

    badge_display_set_my_id(test_value, false);
    badge_display_set_target_id((uint8_t)(test_value << 1), false);

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

        if ((now_ms - last_pattern_ms) >= 500U) {
            last_pattern_ms = now_ms;
            test_value <<= 1;
            if (test_value == 0 || test_value > 0x20U) {
                test_value = 1;
            }
            badge_display_set_my_id(test_value, false);
            badge_display_set_target_id((uint8_t)(0x3fU ^ test_value), false);
        }

        badge_display_tick();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
