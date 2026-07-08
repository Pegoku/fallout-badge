# Firmware Implementation Notes

## Current Status

Started Phase 1 and Phase 2 from `ImprovedPlan.md`.

Implemented:

- Native ESP-IDF project scaffold.
- ESP32-C3 devboard pin map toggle for the two supported mini-board pinouts.
- Initial charlieplex display driver for My ID and Send ID LEDs.
- Configurable logical-to-physical LED order arrays.
- Fixed SLED/RLED control and pulse helpers.
- Initial debounced button polling for IDUp, IDDown, and Action.
- Basic bring-up `app_main` that scans LED patterns and logs button events.

## Build Status

Native ESP-IDF is installed system-wide:

- ESP-IDF package: `esp-idf 6.0.1-1`.
- ESP-IDF path: `/opt/esp-idf`.
- Build tool: `/opt/esp-idf/tools/idf.py`.

`idf.py` is not on `PATH` until the ESP-IDF environment is sourced. Use:

```sh
. /opt/esp-idf/export.sh
```

Verified build command:

```sh
. /opt/esp-idf/export.sh && idf.py set-target esp32c3 && idf.py build
```

Build result:

- `build/fallout_badge.bin` generated successfully.
- App binary size: about `0x23350` bytes.
- Smallest app partition free space: about 86%.

Known environment warnings:

- Git reports dubious ownership for `/opt/esp-idf` because the package files are owned by `nobody`. The build still completes.
- ESP-IDF reports a few upstream Kconfig default-value notifications. They do not block this project build.

## Build And Flash Commands

From this directory, in a new shell:

```sh
. /opt/esp-idf/export.sh
idf.py set-target esp32c3
idf.py build
idf.py flash monitor
```

## Board Pinout Selection

The badge maps logical signals by physical connector position. Both supported
boards use the same positions, but the GPIO labels differ.

Default board:

```text
ESP32-C3 mini:       5V GND 3.3V 10 9 8 7 6 5 4 3 2 1 0
```

Alternative board:

```text
ESP32-C3 Super Mini: 5V GND 3.3V 4 3 2 1 20 10 9 8 7 6 5
```

To change the board:

```sh
. /opt/esp-idf/export.sh
idf.py menuconfig
```

Then select:

```text
Fallout Badge -> ESP32-C3 devboard pinout
```

The default can also be changed in `sdkconfig.defaults`.

Logical signal order across the 11 GPIO positions:

```text
Action IDDown IDUp SID3 SID2 SID1 MID2 MID1 MID3 RLED SLED
```

## Hardware Bring-Up Notes

- Buttons are currently configured as active-low with internal pull-ups.
- Some selected GPIOs are boot strapping sensitive depending on the board. Test reset behavior while holding each button.
- UART-labelled pins may be used for badge signals depending on the selected pinout, so serial logging should stay on USB Serial/JTAG rather than UART.
- Charlieplex LED 0 is the rightmost LED.
- The measured Call ID scan appeared as `4 5 0 1 2 3`, so both LED groups default to order `{4, 5, 2, 3, 0, 1}` to display visible LED order `0 1 2 3 4 5`.
