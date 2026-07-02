# Fallout Badge Firmware Plan

## Feasibility Review

The feature set is feasible on the XIAO ESP32-C3 with ESP-NOW and FreeRTOS/ESP-IDF. The current hardware appears to fit the required signals exactly:

| Function | Net | XIAO / ESP32-C3 GPIO |
| --- | --- | --- |
| My ID charlieplex A | MID1 | GPIO3 / D1 |
| My ID charlieplex B | MID2 | GPIO4 / D2 |
| My ID charlieplex C | MID3 | GPIO2 / D0 |
| Send ID charlieplex A | SID1 | GPIO5 / D3 |
| Send ID charlieplex B | SID2 | GPIO6 / D4 |
| Send ID charlieplex C | SID3 | GPIO7 / D5 |
| Receive indicator LED | RLED | GPIO21 / D6 / TX |
| Send indicator LED | SLED | GPIO20 / D7 / RX |
| ID up button | IDUp | GPIO8 / D8 |
| ID down button | IDDown | GPIO9 / D9 |
| Action button | Action | GPIO10 / D10 |

Main risks and improvements:

1. GPIO8 and GPIO9 can affect ESP32-C3 boot strapping depending on board wiring and pull state. Validate that the buttons do not hold the chip in a bad boot mode when pressed during reset.
2. GPIO20/GPIO21 are UART RX/TX pins. Using them for LEDs is fine, but serial logging over those pins should be disabled or moved to USB CDC during normal firmware.
3. Automatic ID assignment cannot be guaranteed by simply pinging random IDs because two badges can choose the same ID at the same time. Use a probe/claim handshake with random backoff and store the accepted ID in NVS.
4. ESP-NOW is appropriate for short low-latency packets, but it is not guaranteed delivery. Add sequence numbers, acknowledgements for call control packets, and tolerate dropped realtime button-duration packets.
5. Charlieplexing and button scanning must be coordinated so display refresh does not cause ghosting or accidental button reads. Keep LED refresh in one task/driver and keep buttons on independent GPIO inputs with debounce.

## Firmware Stack

Use ESP-IDF with FreeRTOS. PlatformIO may still be used as the build wrapper if desired, but the code should be structured as ESP-IDF components:

- `app_main`: initializes NVS, GPIO, display, buttons, ESP-NOW, and app state.
- `drivers/display`: charlieplex refresh and fixed indicator LEDs.
- `drivers/buttons`: debounced button events with press, release, hold, and chord detection.
- `comms/espnow`: peer setup, packet encode/decode, send queue, receive callback.
- `app/state_machine`: all badge modes and transitions.
- `app/id_manager`: manual ID selection, automatic ID probing, NVS persistence.
- `app/call_session`: call setup, accept/reject/end, realtime message exchange, inactivity timeout.

## Core Data Model

IDs are 6-bit values:

- `0`: reserved for "auto assign / unset"; never used as a normal badge ID.
- `1..63`: valid badge IDs.
- Broadcast ESP-NOW peer is used for discovery, ID probing, and initial call requests.

Runtime state:

- `my_id`: current local badge ID.
- `target_id`: selected outgoing call target.
- `mode`: current UI state.
- `active_call_peer_id`: remote ID for the active call, if any.
- `last_activity_ms`: updated by accepted call traffic and local call input.
- `pending_call`: incoming caller ID and timeout data.

Persistent NVS values:

- Last confirmed `my_id`.
- Optional firmware/protocol version for future migrations.

## ESP-NOW Protocol

Use a compact fixed binary packet. Every packet includes:

- `magic`: identifies this project.
- `version`: protocol version.
- `type`: packet type.
- `src_id`: sender badge ID.
- `dst_id`: destination badge ID, or `0` for broadcast.
- `seq`: monotonically increasing packet sequence.
- `payload`: packet-specific data.

Packet types:

- `ID_PROBE`: asks whether an ID is already in use.
- `ID_PROBE_ACK`: sent by a badge that owns the probed ID.
- `ID_CLAIM`: announces that a badge is claiming an ID after a quiet probe window.
- `CALL_REQUEST`: asks a target badge to start a call.
- `CALL_ACCEPT`: accepts a pending call.
- `CALL_REJECT`: rejects a pending call.
- `CALL_END`: ends an active call.
- `CALL_INPUT_RAW`: sends an action-button duration in milliseconds.
- `CALL_INPUT_SHORT`: sends a short symbol.
- `CALL_INPUT_LONG`: sends a long symbol.
- `PING`: optional presence/keepalive.

Reliability rules:

- Control packets (`CALL_REQUEST`, `CALL_ACCEPT`, `CALL_REJECT`, `CALL_END`, `ID_CLAIM`) are resent a small fixed number of times until acknowledged or timed out.
- Realtime input packets may be sent once to preserve responsiveness.
- Duplicate packets are ignored using `(src_id, seq)`.

## UI State Machine

### Boot

1. Initialize drivers and ESP-NOW.
2. Load `my_id` from NVS.
3. If no saved ID exists, enter `MY_ID_INPUT` with value `0`.
4. If a saved ID exists, display it on My ID LEDs and enter `MAIN`.

### My ID Input

Purpose: choose this badge's own ID.

Display:

- My ID LEDs blink while value is `0`.
- My ID LEDs show the selected 6-bit value for `1..63`.
- Send ID LEDs stay off.

Controls:

- `IDUp`: increment selected ID, wrapping `63 -> 0`.
- `IDDown`: decrement selected ID, wrapping `0 -> 63`.
- `Action` short press:
  - If selected ID is `1..63`, save it and enter `MAIN`.
  - If selected ID is `0`, run automatic ID assignment.

Automatic ID assignment:

1. Pick a random candidate from `1..63`.
2. Broadcast `ID_PROBE(candidate)`.
3. Wait a randomized probe window.
4. If no `ID_PROBE_ACK` is received, broadcast `ID_CLAIM(candidate)`.
5. Wait a second short randomized window.
6. If no conflict appears, save the ID and enter `MAIN`.
7. If a conflict appears, try a different candidate.

### Main

Purpose: idle, show this badge's ID, and listen for calls.

Display:

- My ID LEDs show `my_id`.
- Send ID LEDs off.
- SLED/RLED off except for brief receive/send activity pulses.

Controls:

- `IDUp` or `IDDown` short press: enter `WHO_TO_CALL`.
- Hold `IDUp + IDDown` for 3 seconds: enter `MY_ID_INPUT`.

Comms:

- Listen for `CALL_REQUEST` with `dst_id == my_id`.
- On valid incoming call, store caller ID and enter `CALL_RECEIVED`.

### Call Received

Purpose: accept or reject an incoming call.

Display:

- Blink SLED and RLED together.
- My ID LEDs continue showing `my_id`.
- Send ID LEDs show caller ID.

Controls:

- `Action` short press: send `CALL_ACCEPT`, enter `CALLING`.
- `Action` long press: send `CALL_REJECT`, enter `MAIN`.

Timeout:

- If the user does not respond within a configurable timeout, send `CALL_REJECT` and enter `MAIN`.

### Who To Call

Purpose: choose the remote badge ID.

Display:

- My ID LEDs show `my_id`.
- Send ID LEDs show selected `target_id`.

Controls:

- `IDUp`: increment target ID, wrapping `63 -> 1`.
- `IDDown`: decrement target ID, wrapping `1 -> 63`.
- `Action` short press: send `CALL_REQUEST`, enter `CALLING`.
- `Action` long press: clear target ID and return to `MAIN`.

Validation:

- Do not allow `target_id == 0`.
- If `target_id == my_id`, blink both ID groups as an error and stay in `WHO_TO_CALL`.

### Calling

Purpose: establish or run a bidirectional call.

Substates:

- `OUTGOING_WAIT_ACCEPT`: call request sent, waiting for accept/reject.
- `ACTIVE`: remote accepted; both badges can exchange messages.

Display:

- My ID LEDs show `my_id`.
- Send ID LEDs show remote ID.
- Pulse SLED on local send.
- Pulse RLED on remote receive.

Controls in `OUTGOING_WAIT_ACCEPT`:

- Hold `IDUp + IDDown`: send `CALL_END`, enter `MAIN`.

Controls in `ACTIVE`:

- `Action` press/release: send `CALL_INPUT_RAW` with press duration in milliseconds.
- `IDDown` short press: send `CALL_INPUT_SHORT`.
- `IDUp` short press: send `CALL_INPUT_LONG`.
- Hold `IDUp + IDDown`: send `CALL_END`, enter `MAIN`.

Timeouts:

- If the outgoing call is rejected or times out, enter `MAIN`.
- If there is 15 seconds of inactivity in `ACTIVE`, send `CALL_END` and enter `MAIN`.
- The inactivity timeout should be configurable with a compile-time constant.

## Display Driver Plan

Charlieplexing:

- Each 3-pin LED group controls 6 LEDs.
- Only one charlieplexed LED should be driven at a time per group.
- To light LED `n`, set one pin high, one pin low, and all other group pins high impedance.
- Refresh both 6-LED groups at a stable interval from a single display task or timer.

Display API:

- `display_set_my_id(uint8_t id, bool blink)`
- `display_set_target_id(uint8_t id, bool blink)`
- `display_pulse_send()`
- `display_pulse_receive()`
- `display_set_call_alert(bool enabled)`
- `display_tick()` if implemented without a dedicated task.

Implementation note:

- Start with a simple blocking scan during bring-up, then move refresh to a FreeRTOS task once button and comms code exists.

## Button Driver Plan

Inputs:

- `IDUp`
- `IDDown`
- `Action`

Events:

- `BUTTON_PRESS`
- `BUTTON_RELEASE`
- `BUTTON_SHORT`
- `BUTTON_LONG`
- `BUTTON_HOLD_3S`
- `BUTTON_CHORD_UP_DOWN_3S`

Timing constants:

- Debounce: 20-40 ms.
- Long press: 600-1000 ms.
- Mode-change chord: 3000 ms.

The button driver should publish events to a FreeRTOS queue. The state machine consumes events and owns all interpretation.

## Implementation Milestones

### Phase 1: Project Scaffold

- Add ESP-IDF project files.
- Configure target for ESP32-C3/XIAO.
- Enable USB CDC logging if available.
- Add `sdkconfig.defaults` with Wi-Fi/ESP-NOW-friendly defaults.
- Confirm the firmware builds and boots.

Done when:

- A minimal firmware builds.
- Boot logs are visible.
- The app prints firmware version and reset reason.

### Phase 2: Hardware Bring-Up

- Define the GPIO pin map from the PCB.
- Implement fixed SLED/RLED control.
- Implement charlieplex LED test patterns.
- Implement button input with debounce and event logging.

Done when:

- Every LED can be lit individually.
- Every button reports short and long presses.
- Pressing buttons during reset does not break boot, or the risk is documented with a hardware workaround.

### Phase 3: Local UI State Machine

- Implement `MY_ID_INPUT`, `MAIN`, `WHO_TO_CALL`, and local transitions.
- Implement NVS save/load for `my_id`.
- Add display rendering for IDs and blinking/error states.

Done when:

- A single badge can select and persist its own ID.
- A target ID can be selected and cancelled.
- All local controls match the state machine.

### Phase 4: ESP-NOW Transport

- Initialize Wi-Fi station mode and ESP-NOW.
- Add broadcast peer.
- Implement packet struct, sequence numbers, duplicate filtering, and receive queue.
- Add debug command/log mode for sending test packets.

Done when:

- Two badges can exchange basic `PING` packets.
- SLED/RLED pulse on transmit/receive.
- Duplicate filtering is verified.

### Phase 5: ID Assignment

- Implement `ID_PROBE`, `ID_PROBE_ACK`, and `ID_CLAIM`.
- Add randomized backoff and conflict handling.
- Persist the assigned ID.

Done when:

- Multiple badges powered on together usually pick unique IDs.
- Manual ID conflicts are detectable and logged.

### Phase 6: Call Setup

- Implement `CALL_REQUEST`, `CALL_ACCEPT`, `CALL_REJECT`, and `CALL_END`.
- Add outgoing wait, incoming call alert, accept/reject controls, and call cancellation.
- Add retries for control packets.

Done when:

- Badge A can call badge B.
- Badge B can accept or reject.
- Both badges return to `MAIN` on reject, timeout, or call end.

### Phase 7: Realtime Call Input

- Implement raw action-button duration messages.
- Implement short and long symbol messages from `IDDown` and `IDUp`.
- Keep call traffic non-blocking with send/receive queues.
- Add 15-second inactivity timeout.

Done when:

- Inputs can be exchanged in both directions during an active call.
- Sending does not block receiving.
- Inactivity and manual end both close the call cleanly.

### Phase 8: Polish and Hardening

- Tune button timings and LED refresh rate.
- Reduce logging for release builds.
- Add packet version checks.
- Add basic fault handling for ESP-NOW send failures.
- Add a simple diagnostics mode if flash space allows.

Done when:

- Two or more badges can run through the full flow repeatedly.
- Cold boot, reset, and reconnect behavior are predictable.
- Known limitations are documented in `README.md`.

## Test Plan

Single badge tests:

- Boot with no saved ID.
- Manual ID selection for `1`, `63`, and wraparound.
- Auto ID path with no other badges present.
- Button short, long, and chord detection.
- All LEDs and binary display values.
- NVS persistence across reset.

Two badge tests:

- Manual unique IDs and ping.
- Incoming call accept.
- Incoming call reject.
- Outgoing call timeout.
- Manual call end from either side.
- Raw duration send accuracy within acceptable tolerance.
- Short/long symbol exchange in both directions.

Multi badge tests:

- Auto ID assignment with three or more badges.
- Two badges attempting the same ID.
- One badge receiving a call while another call request is nearby.
- Packet loss simulation by increasing distance or shielding one badge.

## Open Questions

- Should the raw action-button duration be displayed only with SLED/RLED pulses, or should received durations be rendered as blink lengths on RLED?
- Should active calls be strictly one-to-one, rejecting all other callers until the call ends? - Yes, when on a call, it should put on hold all other calls, and if someone calls them some of the LEDs will do like a little dance (you're free to choose which ones may be used)
- Should manual ID conflicts be prevented at selection time or only detected when calls fail? - Yes, when setting an ID it should try to avoid having multiple devices with the same ID
- Should the final firmware use PlatformIO, native ESP-IDF, or both? What are the PROS/CONS of each?
- What exact LED order corresponds to bit positions 0 through 5 for each charlieplex group? It should be selectable an order in an array, so I can set sth like myidleds[0,1,2,5,4,3] and it will set the order)

