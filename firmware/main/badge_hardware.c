#include "badge_hardware.h"

#include "sdkconfig.h"

/*
 * Logical badge nets are wired to physical connector positions. The two
 * supported devboards keep those positions in the same order, but label the
 * GPIOs differently.
 *
 * Position order after power pins, using the C3 mini silk order as reference:
 *   P0 P1 P2 P3 P4 P5 P6 P7 P8 P9 P10
 *
 * Badge net order on those positions:
 *   Action IDDown IDUp SID3 SID2 SID1 MID2 MID1 MID3 RLED SLED
 */

#if CONFIG_BADGE_BOARD_PINOUT_C3_SUPER_MINI
#define BADGE_GPIO_P0 GPIO_NUM_4
#define BADGE_GPIO_P1 GPIO_NUM_3
#define BADGE_GPIO_P2 GPIO_NUM_2
#define BADGE_GPIO_P3 GPIO_NUM_1
#define BADGE_GPIO_P4 GPIO_NUM_20
#define BADGE_GPIO_P5 GPIO_NUM_10
#define BADGE_GPIO_P6 GPIO_NUM_9
#define BADGE_GPIO_P7 GPIO_NUM_8
#define BADGE_GPIO_P8 GPIO_NUM_7
#define BADGE_GPIO_P9 GPIO_NUM_6
#define BADGE_GPIO_P10 GPIO_NUM_5
#define BADGE_PINOUT_NAME "ESP32-C3 Super Mini"
#else
#define BADGE_GPIO_P0 GPIO_NUM_10
#define BADGE_GPIO_P1 GPIO_NUM_9
#define BADGE_GPIO_P2 GPIO_NUM_8
#define BADGE_GPIO_P3 GPIO_NUM_7
#define BADGE_GPIO_P4 GPIO_NUM_6
#define BADGE_GPIO_P5 GPIO_NUM_5
#define BADGE_GPIO_P6 GPIO_NUM_4
#define BADGE_GPIO_P7 GPIO_NUM_3
#define BADGE_GPIO_P8 GPIO_NUM_2
#define BADGE_GPIO_P9 GPIO_NUM_1
#define BADGE_GPIO_P10 GPIO_NUM_0
#define BADGE_PINOUT_NAME "ESP32-C3 mini"
#endif

const badge_pin_map_t BADGE_PINS = {
    .my_id = {
        BADGE_GPIO_P7, /* MID1 */
        BADGE_GPIO_P6, /* MID2 */
        BADGE_GPIO_P8, /* MID3 */
    },
    .send_id = {
        BADGE_GPIO_P5, /* SID1 */
        BADGE_GPIO_P4, /* SID2 */
        BADGE_GPIO_P3, /* SID3 */
    },
    .receive_led = BADGE_GPIO_P9,  /* RLED */
    .send_led = BADGE_GPIO_P10,    /* SLED */
    .button_up = BADGE_GPIO_P2,    /* IDUp */
    .button_down = BADGE_GPIO_P1,  /* IDDown */
    .button_action = BADGE_GPIO_P0,
};

const char *badge_hardware_pinout_name(void)
{
    return BADGE_PINOUT_NAME;
}
