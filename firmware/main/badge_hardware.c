#include "badge_hardware.h"

#include "sdkconfig.h"

/*
 * Logical badge nets are defined from the default XIAO ESP32-C3 schematic.
 * The Super Mini option remaps those same XIAO GPIO signal assignments to the
 * matching GPIOs on the alternate board.
 *
 * The default XIAO ESP32-C3 footprint uses GPIO20/GPIO21 for the two
 * discrete indicators. Those pins are labelled RX/TX, but the firmware keeps
 * console logging on USB Serial/JTAG so they can be used as normal GPIOs.
 */

#if CONFIG_BADGE_BOARD_PINOUT_C3_SUPER_MINI
#define BADGE_GPIO_MID1 GPIO_NUM_6
#define BADGE_GPIO_MID2 GPIO_NUM_7
#define BADGE_GPIO_MID3 GPIO_NUM_5
#define BADGE_GPIO_SID1 GPIO_NUM_8
#define BADGE_GPIO_SID2 GPIO_NUM_9
#define BADGE_GPIO_SID3 GPIO_NUM_10
#define BADGE_GPIO_RLED GPIO_NUM_20
#define BADGE_GPIO_SLED GPIO_NUM_1
#define BADGE_GPIO_IDUP GPIO_NUM_2
#define BADGE_GPIO_IDDOWN GPIO_NUM_3
#define BADGE_GPIO_ACTION GPIO_NUM_4
#define BADGE_PINOUT_NAME "ESP32-C3 Super Mini"
#else
#define BADGE_GPIO_MID1 GPIO_NUM_3
#define BADGE_GPIO_MID2 GPIO_NUM_4
#define BADGE_GPIO_MID3 GPIO_NUM_2
#define BADGE_GPIO_SID1 GPIO_NUM_5
#define BADGE_GPIO_SID2 GPIO_NUM_6
#define BADGE_GPIO_SID3 GPIO_NUM_7
#define BADGE_GPIO_RLED GPIO_NUM_21
#define BADGE_GPIO_SLED GPIO_NUM_20
#define BADGE_GPIO_IDUP GPIO_NUM_8
#define BADGE_GPIO_IDDOWN GPIO_NUM_9
#define BADGE_GPIO_ACTION GPIO_NUM_10
#define BADGE_PINOUT_NAME "ESP32-C3 mini"
#endif

const badge_pin_map_t BADGE_PINS = {
    .my_id = {
        BADGE_GPIO_MID1,
        BADGE_GPIO_MID2,
        BADGE_GPIO_MID3,
    },
    .send_id = {
        BADGE_GPIO_SID1,
        BADGE_GPIO_SID2,
        BADGE_GPIO_SID3,
    },
    .receive_led = BADGE_GPIO_RLED,
    .send_led = BADGE_GPIO_SLED,
    .button_up = BADGE_GPIO_IDUP,
    .button_down = BADGE_GPIO_IDDOWN,
    .button_action = BADGE_GPIO_ACTION,
};

const char *badge_hardware_pinout_name(void)
{
    return BADGE_PINOUT_NAME;
}
