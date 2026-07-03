# Firmware Implementation Notes

## Current Status

Started Phase 1 and Phase 2 from `ImprovedPlan.md`.

Implemented:

- Native ESP-IDF project scaffold.
- ESP32-C3/XIAO GPIO pin map from the PCB.
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

## Hardware Bring-Up Notes

- Buttons are currently configured as active-low with internal pull-ups.
- GPIO8 and GPIO9 are boot strapping sensitive on ESP32-C3. Test reset behavior while holding IDUp and IDDown.
- GPIO20/GPIO21 are used for SLED/RLED, so serial logging should stay on USB CDC rather than UART.
- The current charlieplex physical LED order defaults to `{0, 1, 2, 3, 4, 5}` for both groups and should be adjusted after testing the board.
