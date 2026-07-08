# Fallout Badge Firmware

Firmware for the ESP32-C3 Fallout badge.

Current firmware status: hardware bring-up. It cycles every LED group,
pulses SLED/RLED on button events, and logs button presses over USB
Serial/JTAG.

## Requirements

- ESP-IDF 6.x
- ESP32-C3 devboard connected over USB
- `cmake`, `ninja`, and the ESP-IDF toolchain

On this machine ESP-IDF is installed at:

```sh
/opt/esp-idf
```

Before using `idf.py`, source the ESP-IDF environment:

```sh
. /opt/esp-idf/export.sh
```

## Select Board Pinout

Two ESP32-C3 mini devboard pinouts are supported. They use the same physical
connector positions, but the GPIO numbers printed on the boards differ.

Default:

```text
ESP32-C3 mini:       5V GND 3.3V 10 9 8 7 6 5 4 3 2 1 0
```

Alternative:

```text
ESP32-C3 Super Mini: 5V GND 3.3V 4 3 2 1 20 10 9 8 7 6 5
```

To change the selected board:

```sh
. /opt/esp-idf/export.sh
idf.py menuconfig
```

Then choose:

```text
Fallout Badge -> ESP32-C3 devboard pinout
```

The default board is also set in `sdkconfig.defaults`:

```text
CONFIG_BADGE_BOARD_PINOUT_C3_MINI=y
```

## Build

```sh
. /opt/esp-idf/export.sh
idf.py set-target esp32c3
idf.py build
```

The output firmware is:

```text
build/fallout_badge.bin
```

## Flash And Monitor

With one ESP32-C3 connected:

```sh
. /opt/esp-idf/export.sh
idf.py flash monitor
```

If multiple serial devices are connected, pass the port explicitly:

```sh
idf.py -p /dev/ttyACM0 flash monitor
```

Exit the monitor with `Ctrl+]`.

## Bring-Up Behavior

After flashing:

- The firmware logs the selected pinout at boot.
- My ID LEDs are tested one at a time, then all at once.
- Call ID LEDs are tested one at a time, then all at once.
- SLED and RLED are tested individually, then together.
- Pressing `Action` pulses SLED.
- Pressing `IDUp` or `IDDown` pulses RLED.
- Button press, release, short, long, and `IDUp + IDDown` chord events are logged.

## Hardware Notes

- Buttons are configured as active-low with internal pull-ups.
- Some GPIOs are boot strapping pins depending on the selected board. Test reset behavior while holding each button.
- UART-labelled pins may be used as badge signals, so console logging is configured for USB Serial/JTAG instead of UART.
- Charlieplex LED 0 is the rightmost LED.
- The measured charlieplex scan order is `0 1 4 5 2 3`, so both LED groups default to order `{0, 1, 4, 5, 2, 3}` in `main/badge_display.c`.

## Useful Commands

Clean and rebuild:

```sh
idf.py fullclean
idf.py build
```

Open config:

```sh
idf.py menuconfig
```

Show configured badge pinout symbols:

```sh
rg "BADGE_BOARD_PINOUT" sdkconfig
```
