# Fallout Badge Firmware

Firmware for the ESP32-C3 Fallout badge.

Current firmware status: full ESP-NOW application firmware. It stores a local
badge ID, validates claimed IDs over ESP-NOW, selects a call target, handles
incoming/outgoing calls, and exchanges short/long/raw call inputs.

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

For the default XIAO ESP32-C3 schematic, the ID LEDs and buttons use the
working `GPIO10..GPIO2` mapping, while the two discrete indicators are wired
separately as `RLED=GPIO21` and `SLED=GPIO20`.

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

## Usage

After flashing:

- The firmware logs the selected pinout at boot.
- The firmware logs the concrete GPIO map used for LEDs and buttons.
- If no local ID is saved, the badge enters My ID input.
- If a local ID is saved, the badge enters Main mode and shows it on My ID LEDs.

Controls:

- My ID input: `IDUp`/`IDDown` changes the local ID, including `0`.
- My ID input: hold `IDUp` or `IDDown` for 0.8 seconds to auto-repeat with acceleration.
- My ID input: `Action` validates and saves the selected ID. Selecting `0` runs automatic assignment.
- Main: `IDUp` or `IDDown` enters Who To Call mode.
- Main or Who To Call: hold `IDUp + IDDown` for 3 seconds to re-enter My ID input.
- Who To Call: `IDUp`/`IDDown` changes the target ID.
- Who To Call: hold `IDUp` or `IDDown` for 0.8 seconds to auto-repeat with acceleration.
- Who To Call: `Action` sends a call request.
- Who To Call: long-press `Action` cancels back to Main.
- Incoming call: short-press `Action` accepts.
- Incoming call: long-press `Action` rejects.
- Outgoing wait or active call: hold `IDUp + IDDown` for 3 seconds to end/cancel.
- Active call: holding `Action` live-streams the raw input; SLED turns on while pressed and the receiver's RLED mirrors it until release.
- Active call: short-press `IDDown` sends a short visible symbol.
- Active call: short-press `IDUp` sends a long visible symbol.

Display behavior:

- My ID LEDs show the local badge ID.
- Send ID LEDs show the selected target, incoming caller, or active peer.
- The editable LED group blinks in My ID input and Who To Call modes.
- SLED pulses on local sends and stays on while holding `Action` in an active call.
- SLED/RLED hold for readable short and long symbol durations during active-call inputs.
- RLED mirrors received raw-duration inputs.
- Incoming calls blink SLED and RLED together.

## Hardware Notes

- Buttons are configured as active-low with internal pull-ups.
- Some GPIOs are boot strapping pins depending on the selected board. Test reset behavior while holding each button.
- The default XIAO ESP32-C3 schematic puts RLED/SLED on GPIO21/GPIO20. These are UART-labelled pins, so console logging is configured for USB Serial/JTAG instead of UART.
- Charlieplex LED 0 is the rightmost LED.
- The measured My ID scan appeared as `4 3 2 5 1 0`, so My ID defaults to order `{1, 0, 2, 5, 4, 3}` in `main/badge_display.c`.
- The measured Call ID scan appeared as `4 5 0 1 2 3`, so Call ID defaults to order `{4, 5, 2, 3, 0, 1}` in `main/badge_display.c`.

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
