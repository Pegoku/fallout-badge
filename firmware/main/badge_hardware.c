#include "badge_hardware.h"

const badge_pin_map_t BADGE_PINS = {
    .my_id = {
        GPIO_NUM_3, /* MID1 */
        GPIO_NUM_4, /* MID2 */
        GPIO_NUM_2, /* MID3 */
    },
    .send_id = {
        GPIO_NUM_5, /* SID1 */
        GPIO_NUM_6, /* SID2 */
        GPIO_NUM_7, /* SID3 */
    },
    .receive_led = GPIO_NUM_21, /* RLED */
    .send_led = GPIO_NUM_20,    /* SLED */
    .button_up = GPIO_NUM_8,    /* IDUp */
    .button_down = GPIO_NUM_9,  /* IDDown */
    .button_action = GPIO_NUM_10,
};
