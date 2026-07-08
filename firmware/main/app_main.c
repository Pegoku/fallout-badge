#include <stdbool.h>
#include <stdint.h>

#include "badge_buttons.h"
#include "badge_comms.h"
#include "badge_display.h"
#include "badge_hardware.h"
#include "badge_storage.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "fallout_badge";

#define MAIN_LOOP_MS 10U
#define ID_PROBE_WINDOW_MS 700U
#define ID_AUTO_RETRY_MS 250U
#define INCOMING_CALL_TIMEOUT_MS 15000U
#define OUTGOING_CALL_TIMEOUT_MS 15000U
#define CALL_INACTIVITY_TIMEOUT_MS 30000U
#define ALERT_BLINK_MS 300U
#define RAW_DURATION_MAX_MS 5000U
#define INPUT_REPEAT_START_MS 800U
#define CALL_INPUT_SHORT_MS 220U
#define CALL_INPUT_LONG_MS 650U

#define CALL_INPUT_RAW_ARG_DURATION 0U
#define CALL_INPUT_RAW_ARG_START 1U
#define CALL_INPUT_RAW_ARG_STOP 2U

#define RELIABLE_PACKET_COUNT 12U
#define RELIABLE_RETRY_MS 90U
#define RELIABLE_MAX_ATTEMPTS 6U

#define SYMBOL_QUEUE_COUNT 16U
#define SYMBOL_GAP_MS 160U

typedef enum {
    APP_MODE_MY_ID_INPUT = 0,
    APP_MODE_ID_PROBE_WAIT,
    APP_MODE_MAIN,
    APP_MODE_WHO_TO_CALL,
    APP_MODE_CALL_RECEIVED,
    APP_MODE_OUTGOING_WAIT,
    APP_MODE_CALL_ACTIVE,
} app_mode_t;

typedef struct {
    bool active;
    badge_packet_t packet;
    uint8_t attempts;
    uint32_t next_retry_ms;
} reliable_packet_t;

typedef struct {
    badge_packet_type_t type;
    uint16_t duration_ms;
} queued_symbol_t;

typedef struct {
    app_mode_t mode;
    uint8_t my_id;
    uint8_t selected_my_id;
    uint8_t target_id;
    uint8_t pending_peer_id;
    uint8_t active_peer_id;
    uint8_t probe_candidate_id;
    bool auto_assigning;
    bool probe_conflict_seen;
    bool action_pressed;
    bool call_alert_on;
    bool input_repeat_active;
    bool queued_symbol_waiting_ack;
    badge_button_id_t input_repeat_button;
    uint8_t input_repeat_count;
    uint32_t mode_started_ms;
    uint32_t probe_deadline_ms;
    uint32_t next_auto_retry_ms;
    uint32_t last_activity_ms;
    uint32_t call_deadline_ms;
    uint32_t action_press_started_ms;
    uint32_t send_symbol_until_ms;
    uint32_t next_symbol_start_ms;
    uint32_t receive_raw_until_ms;
    uint32_t last_alert_toggle_ms;
    uint32_t next_input_repeat_ms;
    uint16_t last_packet_seq_by_id[64];
    reliable_packet_t reliable_packets[RELIABLE_PACKET_COUNT];
    uint16_t queued_symbol_seq;
    queued_symbol_t symbol_queue[SYMBOL_QUEUE_COUNT];
    uint8_t symbol_queue_head;
    uint8_t symbol_queue_tail;
    uint8_t symbol_queue_count;
} app_state_t;

static app_state_t s_app;

static void refresh_call_activity(uint32_t now);
static uint16_t call_input_symbol_duration(badge_packet_type_t type);
static esp_err_t send_reliable_packet(badge_packet_t *packet, bool pulse_indicator);

static TickType_t delay_ticks_at_least_one(uint32_t ms)
{
    const TickType_t ticks = pdMS_TO_TICKS(ms);
    return ticks > 0 ? ticks : 1;
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool deadline_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static uint8_t wrap_id_up(uint8_t id, bool allow_zero)
{
    const uint8_t max_id = 63U;
    if (id >= max_id) {
        return allow_zero ? 0U : 1U;
    }
    return (uint8_t)(id + 1U);
}

static uint8_t wrap_id_down(uint8_t id, bool allow_zero)
{
    if (id == 0U) {
        return allow_zero ? 63U : 1U;
    }
    if (!allow_zero && id == 1U) {
        return 63U;
    }
    return (uint8_t)(id - 1U);
}

static uint8_t random_badge_id(void)
{
    return (uint8_t)((esp_random() % 63U) + 1U);
}

static uint32_t input_repeat_interval_ms(uint8_t repeat_count)
{
    if (repeat_count < 4U) {
        return 700U;
    }
    if (repeat_count < 10U) {
        return 500U;
    }
    if (repeat_count < 18U) {
        return 320U;
    }
    if (repeat_count < 30U) {
        return 220U;
    }
    return 140U;
}

static bool mode_supports_input_repeat(app_mode_t mode)
{
    return mode == APP_MODE_MY_ID_INPUT || mode == APP_MODE_WHO_TO_CALL;
}

static void stop_input_repeat(void)
{
    s_app.input_repeat_active = false;
    s_app.input_repeat_count = 0;
}

static void clear_symbol_queue(void)
{
    s_app.symbol_queue_head = 0;
    s_app.symbol_queue_tail = 0;
    s_app.symbol_queue_count = 0;
    s_app.next_symbol_start_ms = 0;
    s_app.queued_symbol_waiting_ack = false;
    s_app.queued_symbol_seq = 0;
}

static bool enqueue_symbol(badge_packet_type_t type)
{
    if (s_app.symbol_queue_count >= SYMBOL_QUEUE_COUNT) {
        ESP_LOGW(TAG, "symbol queue full, dropping %s",
                 badge_packet_type_name(type));
        return false;
    }

    queued_symbol_t *slot = &s_app.symbol_queue[s_app.symbol_queue_tail];
    slot->type = type;
    slot->duration_ms = call_input_symbol_duration(type);
    s_app.symbol_queue_tail =
        (uint8_t)((s_app.symbol_queue_tail + 1U) % SYMBOL_QUEUE_COUNT);
    s_app.symbol_queue_count++;

    ESP_LOGI(TAG, "queued %s count=%u", badge_packet_type_name(type),
             (unsigned)s_app.symbol_queue_count);
    return true;
}

static bool dequeue_symbol(queued_symbol_t *symbol)
{
    if (symbol == NULL || s_app.symbol_queue_count == 0U) {
        return false;
    }

    *symbol = s_app.symbol_queue[s_app.symbol_queue_head];
    s_app.symbol_queue_head =
        (uint8_t)((s_app.symbol_queue_head + 1U) % SYMBOL_QUEUE_COUNT);
    s_app.symbol_queue_count--;
    return true;
}

static void show_selected_my_id(void)
{
    badge_display_set_my_id(s_app.selected_my_id == 0U ? 0x3fU :
                            s_app.selected_my_id, true);
}

static void show_target_id(void)
{
    badge_display_set_target_id(s_app.target_id, true);
}

static void change_selected_my_id(badge_button_id_t button)
{
    if (button == BADGE_BUTTON_UP) {
        s_app.selected_my_id = wrap_id_up(s_app.selected_my_id, true);
    } else if (button == BADGE_BUTTON_DOWN) {
        s_app.selected_my_id = wrap_id_down(s_app.selected_my_id, true);
    }
    show_selected_my_id();
}

static void change_target_id(badge_button_id_t button)
{
    if (button == BADGE_BUTTON_UP) {
        do {
            s_app.target_id = wrap_id_up(s_app.target_id, false);
        } while (s_app.target_id == s_app.my_id);
    } else if (button == BADGE_BUTTON_DOWN) {
        do {
            s_app.target_id = wrap_id_down(s_app.target_id, false);
        } while (s_app.target_id == s_app.my_id);
    }
    show_target_id();
}

static void repeat_input_step(uint32_t now)
{
    if (s_app.mode == APP_MODE_MY_ID_INPUT) {
        change_selected_my_id(s_app.input_repeat_button);
    } else if (s_app.mode == APP_MODE_WHO_TO_CALL) {
        change_target_id(s_app.input_repeat_button);
    }

    if (s_app.input_repeat_count < UINT8_MAX) {
        s_app.input_repeat_count++;
    }
    s_app.next_input_repeat_ms =
        now + input_repeat_interval_ms(s_app.input_repeat_count);
}

static void start_input_repeat(badge_button_id_t button, uint32_t now)
{
    if (!mode_supports_input_repeat(s_app.mode) ||
        (button != BADGE_BUTTON_UP && button != BADGE_BUTTON_DOWN)) {
        return;
    }

    s_app.input_repeat_active = true;
    s_app.input_repeat_button = button;
    s_app.input_repeat_count = 0;
    repeat_input_step(now);
}

static void display_idle(void)
{
    badge_display_set_my_id(s_app.my_id, false);
    badge_display_set_target_id(0, false);
    badge_display_set_send_led(false);
    badge_display_set_receive_led(false);
}

static void enter_mode(app_mode_t mode, uint32_t now)
{
    s_app.mode = mode;
    s_app.mode_started_ms = now;
    s_app.call_alert_on = false;
    s_app.last_alert_toggle_ms = now;
    stop_input_repeat();
    if (mode != APP_MODE_CALL_ACTIVE) {
        s_app.send_symbol_until_ms = 0;
        s_app.receive_raw_until_ms = 0;
        clear_symbol_queue();
    }

    switch (mode) {
    case APP_MODE_MY_ID_INPUT:
        show_selected_my_id();
        badge_display_set_target_id(0, false);
        badge_display_set_send_led(false);
        badge_display_set_receive_led(false);
        break;
    case APP_MODE_MAIN:
        s_app.pending_peer_id = 0;
        s_app.active_peer_id = 0;
        s_app.target_id = 1U;
        s_app.action_pressed = false;
        display_idle();
        break;
    case APP_MODE_WHO_TO_CALL:
        if (s_app.target_id == 0U || s_app.target_id == s_app.my_id) {
            s_app.target_id = s_app.my_id == 1U ? 2U : 1U;
        }
        badge_display_set_my_id(s_app.my_id, false);
        show_target_id();
        badge_display_set_send_led(false);
        badge_display_set_receive_led(false);
        break;
    case APP_MODE_CALL_RECEIVED:
        badge_display_set_my_id(s_app.my_id, false);
        badge_display_set_target_id(s_app.pending_peer_id, false);
        break;
    case APP_MODE_OUTGOING_WAIT:
        badge_display_set_my_id(s_app.my_id, false);
        badge_display_set_target_id(s_app.active_peer_id, true);
        badge_display_set_send_led(false);
        badge_display_set_receive_led(false);
        break;
    case APP_MODE_CALL_ACTIVE:
        refresh_call_activity(now);
        badge_display_set_my_id(s_app.my_id, false);
        badge_display_set_target_id(s_app.active_peer_id, false);
        badge_display_set_send_led(false);
        badge_display_set_receive_led(false);
        break;
    case APP_MODE_ID_PROBE_WAIT:
        badge_display_set_my_id(s_app.probe_candidate_id, true);
        badge_display_set_target_id(0, false);
        badge_display_set_send_led(true);
        badge_display_set_receive_led(false);
        break;
    default:
        break;
    }
}

static void send_control(uint8_t dst_id, badge_packet_type_t type, uint8_t arg)
{
    if (dst_id != BADGE_BROADCAST_ID && type != BADGE_PACKET_ACK) {
        badge_packet_t packet;
        esp_err_t err = badge_comms_make_packet(s_app.my_id, dst_id, type, arg,
                                                0, &packet);
        if (err == ESP_OK) {
            err = send_reliable_packet(&packet, true);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "tx reliable control failed %s dst=%u",
                     badge_packet_type_name(type), (unsigned)dst_id);
        }
        return;
    }

    if (badge_comms_send_control(s_app.my_id, dst_id, type, arg) == ESP_OK) {
        badge_display_pulse_send();
        ESP_LOGI(TAG, "tx %s dst=%u arg=%u", badge_packet_type_name(type),
                 (unsigned)dst_id, (unsigned)arg);
    } else {
        ESP_LOGW(TAG, "tx failed %s dst=%u", badge_packet_type_name(type),
                 (unsigned)dst_id);
    }
}

static void refresh_call_activity(uint32_t now)
{
    s_app.last_activity_ms = now;
    s_app.call_deadline_ms = now + CALL_INACTIVITY_TIMEOUT_MS;
}

static void end_call(uint32_t now, const char *reason)
{
    ESP_LOGI(TAG, "ending call: reason=%s peer=%u", reason,
             (unsigned)s_app.active_peer_id);
    send_control(s_app.active_peer_id, BADGE_PACKET_CALL_END, 0);
    enter_mode(APP_MODE_MAIN, now);
}

static uint16_t call_input_symbol_duration(badge_packet_type_t type)
{
    return type == BADGE_PACKET_CALL_INPUT_LONG ? CALL_INPUT_LONG_MS :
                                                  CALL_INPUT_SHORT_MS;
}

static void show_send_symbol(uint32_t now, uint16_t duration_ms)
{
    s_app.send_symbol_until_ms = now + duration_ms;
    badge_display_set_send_led(true);
}

static void show_receive_symbol(uint32_t now, uint16_t duration_ms)
{
    s_app.receive_raw_until_ms = now + duration_ms;
    badge_display_set_receive_led(true);
}

static bool packet_requires_ack(const badge_packet_t *packet)
{
    return packet != NULL &&
           packet->type != BADGE_PACKET_ACK &&
           packet->dst_id != BADGE_BROADCAST_ID;
}

static reliable_packet_t *alloc_reliable_slot(void)
{
    reliable_packet_t *oldest = &s_app.reliable_packets[0];

    for (uint8_t i = 0; i < RELIABLE_PACKET_COUNT; i++) {
        reliable_packet_t *slot = &s_app.reliable_packets[i];
        if (!slot->active) {
            return slot;
        }
        if ((int32_t)(slot->next_retry_ms - oldest->next_retry_ms) < 0) {
            oldest = slot;
        }
    }

    ESP_LOGW(TAG, "reliable queue full, replacing seq=%u",
             (unsigned)oldest->packet.seq);
    if (s_app.queued_symbol_waiting_ack &&
        s_app.queued_symbol_seq == oldest->packet.seq) {
        s_app.queued_symbol_waiting_ack = false;
        s_app.queued_symbol_seq = 0;
    }
    return oldest;
}

static void track_reliable_packet(const badge_packet_t *packet, uint32_t now)
{
    reliable_packet_t *slot = alloc_reliable_slot();
    *slot = (reliable_packet_t){
        .active = true,
        .packet = *packet,
        .attempts = 1,
        .next_retry_ms = now + RELIABLE_RETRY_MS,
    };
}

static void cancel_reliable_packet(uint8_t dst_id, badge_packet_type_t type,
                                   uint8_t arg)
{
    for (uint8_t i = 0; i < RELIABLE_PACKET_COUNT; i++) {
        reliable_packet_t *slot = &s_app.reliable_packets[i];
        if (slot->active &&
            slot->packet.dst_id == dst_id &&
            slot->packet.type == type &&
            slot->packet.arg == arg) {
            ESP_LOGI(TAG, "cancel reliable %s dst=%u seq=%u arg=%u",
                     badge_packet_type_name(type), (unsigned)dst_id,
                     (unsigned)slot->packet.seq, (unsigned)arg);
            slot->active = false;
        }
    }
}

static esp_err_t send_reliable_packet(badge_packet_t *packet, bool pulse_indicator)
{
    const uint32_t now = now_ms();
    esp_err_t err = badge_comms_send_packet(packet);
    if (err == ESP_OK) {
        if (pulse_indicator) {
            badge_display_pulse_send();
        }
        refresh_call_activity(now);
        track_reliable_packet(packet, now);
        ESP_LOGI(TAG, "tx reliable %s dst=%u seq=%u arg=%u duration=%u",
                 badge_packet_type_name(packet->type), (unsigned)packet->dst_id,
                 (unsigned)packet->seq, (unsigned)packet->arg,
                 (unsigned)packet->duration_ms);
    } else {
        ESP_LOGW(TAG, "tx reliable failed %s dst=%u",
                 badge_packet_type_name(packet->type), (unsigned)packet->dst_id);
    }
    return err;
}

static void send_reliable_input(uint8_t dst_id, badge_packet_type_t type,
                                uint8_t arg, uint16_t duration_ms,
                                bool pulse_indicator, uint16_t *seq_out)
{
    badge_packet_t packet;
    esp_err_t err = badge_comms_make_packet(s_app.my_id, dst_id, type, arg,
                                            duration_ms, &packet);
    if (err == ESP_OK) {
        (void)send_reliable_packet(&packet, pulse_indicator);
        if (seq_out != NULL) {
            *seq_out = packet.seq;
        }
    } else if (seq_out != NULL) {
        *seq_out = 0;
    }
}

static void send_ack(const badge_packet_t *packet)
{
    (void)badge_comms_send(s_app.my_id, packet->src_id, BADGE_PACKET_ACK,
                           packet->type, packet->seq);
    ESP_LOGI(TAG, "tx ACK dst=%u seq=%u type=%s", (unsigned)packet->src_id,
             (unsigned)packet->seq, badge_packet_type_name(packet->type));
}

static void handle_ack(const badge_packet_t *packet)
{
    for (uint8_t i = 0; i < RELIABLE_PACKET_COUNT; i++) {
        reliable_packet_t *slot = &s_app.reliable_packets[i];
        if (slot->active &&
            slot->packet.dst_id == packet->src_id &&
            slot->packet.seq == packet->duration_ms &&
            slot->packet.type == packet->arg) {
            ESP_LOGI(TAG, "rx ACK src=%u seq=%u type=%s attempts=%u",
                     (unsigned)packet->src_id, (unsigned)slot->packet.seq,
                     badge_packet_type_name(slot->packet.type),
                     (unsigned)slot->attempts);
            if (s_app.queued_symbol_waiting_ack &&
                s_app.queued_symbol_seq == slot->packet.seq) {
                s_app.queued_symbol_waiting_ack = false;
                s_app.queued_symbol_seq = 0;
            }
            slot->active = false;
            return;
        }
    }
}

static bool packet_is_duplicate(const badge_packet_t *packet)
{
    if (packet->src_id == 0U || packet->src_id > 63U) {
        return false;
    }

    if (s_app.last_packet_seq_by_id[packet->src_id] == packet->seq) {
        return true;
    }

    s_app.last_packet_seq_by_id[packet->src_id] = packet->seq;
    return false;
}

static void start_id_probe(uint8_t candidate_id, bool auto_assigning, uint32_t now)
{
    s_app.probe_candidate_id = candidate_id;
    s_app.auto_assigning = auto_assigning;
    s_app.probe_conflict_seen = false;
    s_app.probe_deadline_ms =
        now + ID_PROBE_WINDOW_MS + (esp_random() % ID_AUTO_RETRY_MS);

    (void)badge_comms_send_control(s_app.my_id, BADGE_BROADCAST_ID,
                                   BADGE_PACKET_ID_PROBE, candidate_id);
    badge_display_pulse_send();
    ESP_LOGI(TAG, "probing badge id %u", (unsigned)candidate_id);
    enter_mode(APP_MODE_ID_PROBE_WAIT, now);
}

static void accept_id(uint8_t id, uint32_t now)
{
    s_app.my_id = id;
    s_app.selected_my_id = id;
    ESP_ERROR_CHECK(badge_storage_save_my_id(id));
    send_control(BADGE_BROADCAST_ID, BADGE_PACKET_ID_CLAIM, id);
    ESP_LOGI(TAG, "badge id set to %u", (unsigned)id);
    enter_mode(APP_MODE_MAIN, now);
}

static bool packet_is_for_me(const badge_packet_t *packet)
{
    return packet->dst_id == BADGE_BROADCAST_ID ||
           (s_app.my_id != 0U && packet->dst_id == s_app.my_id);
}

static void handle_packet(const badge_packet_t *packet, uint32_t now)
{
    if (packet->src_id == s_app.my_id && s_app.my_id != 0U) {
        return;
    }
    if (!packet_is_for_me(packet)) {
        return;
    }

    ESP_LOGI(TAG, "rx %s src=%u dst=%u arg=%u duration=%u",
             badge_packet_type_name(packet->type), (unsigned)packet->src_id,
             (unsigned)packet->dst_id, (unsigned)packet->arg,
             (unsigned)packet->duration_ms);

    if (packet->type == BADGE_PACKET_ACK) {
        handle_ack(packet);
        return;
    }

    if (packet_requires_ack(packet)) {
        send_ack(packet);
    }

    if (packet_is_duplicate(packet)) {
        return;
    }

    if (packet->type == BADGE_PACKET_ID_PROBE) {
        if (s_app.my_id != 0U && packet->arg == s_app.my_id) {
            send_control(packet->src_id, BADGE_PACKET_ID_PROBE_ACK, s_app.my_id);
        }
        return;
    }

    if (packet->type == BADGE_PACKET_ID_PROBE_ACK) {
        if (s_app.mode == APP_MODE_ID_PROBE_WAIT &&
            packet->arg == s_app.probe_candidate_id) {
            s_app.probe_conflict_seen = true;
            badge_display_pulse_receive();
        }
        return;
    }

    if (packet->type == BADGE_PACKET_ID_CLAIM) {
        if (s_app.my_id != 0U && packet->arg == s_app.my_id) {
            send_control(packet->src_id, BADGE_PACKET_ID_CONFLICT, s_app.my_id);
            badge_display_play_error_dance();
        }
        if (s_app.mode == APP_MODE_ID_PROBE_WAIT &&
            packet->arg == s_app.probe_candidate_id) {
            s_app.probe_conflict_seen = true;
        }
        return;
    }

    if (packet->type == BADGE_PACKET_ID_CONFLICT) {
        if (packet->arg == s_app.my_id || packet->arg == s_app.probe_candidate_id) {
            badge_display_play_error_dance();
            if (s_app.mode == APP_MODE_ID_PROBE_WAIT) {
                s_app.probe_conflict_seen = true;
            }
        }
        return;
    }

    if (s_app.my_id == 0U) {
        return;
    }

    switch ((badge_packet_type_t)packet->type) {
    case BADGE_PACKET_CALL_REQUEST:
        badge_display_pulse_receive();
        if (s_app.mode == APP_MODE_CALL_ACTIVE ||
            s_app.mode == APP_MODE_OUTGOING_WAIT ||
            s_app.mode == APP_MODE_CALL_RECEIVED) {
            send_control(packet->src_id, BADGE_PACKET_CALL_HOLD, s_app.my_id);
            badge_display_play_busy_dance();
            badge_display_set_my_id(s_app.my_id, false);
            badge_display_set_target_id(s_app.active_peer_id, false);
            return;
        }
        s_app.pending_peer_id = packet->src_id;
        enter_mode(APP_MODE_CALL_RECEIVED, now);
        break;

    case BADGE_PACKET_CALL_ACCEPT:
        if (s_app.mode == APP_MODE_OUTGOING_WAIT &&
            packet->src_id == s_app.active_peer_id) {
            badge_display_pulse_receive();
            enter_mode(APP_MODE_CALL_ACTIVE, now);
        }
        break;

    case BADGE_PACKET_CALL_REJECT:
    case BADGE_PACKET_CALL_HOLD:
        if (s_app.mode == APP_MODE_OUTGOING_WAIT &&
            packet->src_id == s_app.active_peer_id) {
            badge_display_pulse_receive();
            badge_display_play_error_dance();
            enter_mode(APP_MODE_MAIN, now);
        }
        break;

    case BADGE_PACKET_CALL_END:
        if ((s_app.mode == APP_MODE_CALL_ACTIVE ||
             s_app.mode == APP_MODE_OUTGOING_WAIT) &&
            packet->src_id == s_app.active_peer_id) {
            badge_display_pulse_receive();
            ESP_LOGI(TAG, "ending call: reason=remote_end peer=%u",
                     (unsigned)s_app.active_peer_id);
            enter_mode(APP_MODE_MAIN, now);
        }
        break;

    case BADGE_PACKET_CALL_INPUT_RAW:
        if (s_app.mode == APP_MODE_CALL_ACTIVE &&
            packet->src_id == s_app.active_peer_id) {
            refresh_call_activity(now);
            if (packet->arg == CALL_INPUT_RAW_ARG_START) {
                s_app.receive_raw_until_ms = 0;
                badge_display_set_receive_led(true);
            } else if (packet->arg == CALL_INPUT_RAW_ARG_STOP) {
                s_app.receive_raw_until_ms = 0;
                badge_display_set_receive_led(false);
            } else {
                uint32_t duration = packet->duration_ms;
                if (duration > RAW_DURATION_MAX_MS) {
                    duration = RAW_DURATION_MAX_MS;
                }
                show_receive_symbol(now, (uint16_t)duration);
            }
        }
        break;

    case BADGE_PACKET_CALL_INPUT_SHORT:
    case BADGE_PACKET_CALL_INPUT_LONG:
        if (s_app.mode == APP_MODE_CALL_ACTIVE &&
            packet->src_id == s_app.active_peer_id) {
            uint16_t duration = packet->duration_ms;
            if (duration == 0U) {
                duration = call_input_symbol_duration(
                    (badge_packet_type_t)packet->type);
            }
            refresh_call_activity(now);
            show_receive_symbol(now, duration);
        }
        break;

    case BADGE_PACKET_PING:
        badge_display_pulse_receive();
        break;

    default:
        break;
    }
}

static void handle_button_event(const badge_button_event_t *event, uint32_t now)
{
    ESP_LOGI(TAG, "button=%s event=%s duration_ms=%lu",
             badge_button_name(event->button), badge_button_event_name(event->type),
             (unsigned long)event->duration_ms);

    if (event->type == BADGE_BUTTON_EVENT_CHORD_UP_DOWN_3S) {
        if (s_app.mode == APP_MODE_MAIN || s_app.mode == APP_MODE_WHO_TO_CALL) {
            s_app.selected_my_id = s_app.my_id;
            enter_mode(APP_MODE_MY_ID_INPUT, now);
            return;
        }
        if (s_app.mode == APP_MODE_CALL_ACTIVE ||
            s_app.mode == APP_MODE_OUTGOING_WAIT) {
            end_call(now, "up_down_chord");
            return;
        }
    }

    switch (s_app.mode) {
    case APP_MODE_MY_ID_INPUT:
        if (event->type == BADGE_BUTTON_EVENT_RELEASE &&
            event->button == s_app.input_repeat_button) {
            stop_input_repeat();
        } else if (event->type == BADGE_BUTTON_EVENT_LONG &&
                   (event->button == BADGE_BUTTON_UP ||
                    event->button == BADGE_BUTTON_DOWN)) {
            start_input_repeat(event->button, now);
        }
        if (event->type == BADGE_BUTTON_EVENT_SHORT) {
            if (event->button == BADGE_BUTTON_UP) {
                change_selected_my_id(event->button);
            } else if (event->button == BADGE_BUTTON_DOWN) {
                change_selected_my_id(event->button);
            } else if (event->button == BADGE_BUTTON_ACTION) {
                if (s_app.selected_my_id == 0U) {
                    start_id_probe(random_badge_id(), true, now);
                } else {
                    start_id_probe(s_app.selected_my_id, false, now);
                }
            }
        }
        break;

    case APP_MODE_MAIN:
        if (event->type == BADGE_BUTTON_EVENT_SHORT &&
            (event->button == BADGE_BUTTON_UP ||
             event->button == BADGE_BUTTON_DOWN)) {
            enter_mode(APP_MODE_WHO_TO_CALL, now);
        }
        break;

    case APP_MODE_WHO_TO_CALL:
        if (event->type == BADGE_BUTTON_EVENT_RELEASE &&
            event->button == s_app.input_repeat_button) {
            stop_input_repeat();
        } else if (event->type == BADGE_BUTTON_EVENT_LONG &&
                   (event->button == BADGE_BUTTON_UP ||
                    event->button == BADGE_BUTTON_DOWN)) {
            start_input_repeat(event->button, now);
        }
        if (event->type == BADGE_BUTTON_EVENT_SHORT) {
            if (event->button == BADGE_BUTTON_UP) {
                change_target_id(event->button);
            } else if (event->button == BADGE_BUTTON_DOWN) {
                change_target_id(event->button);
            } else if (event->button == BADGE_BUTTON_ACTION) {
                if (s_app.target_id == s_app.my_id || s_app.target_id == 0U) {
                    badge_display_play_error_dance();
                } else {
                    s_app.active_peer_id = s_app.target_id;
                    send_control(s_app.active_peer_id, BADGE_PACKET_CALL_REQUEST,
                                 s_app.my_id);
                    enter_mode(APP_MODE_OUTGOING_WAIT, now);
                }
            }
        } else if (event->type == BADGE_BUTTON_EVENT_LONG &&
                   event->button == BADGE_BUTTON_ACTION) {
            enter_mode(APP_MODE_MAIN, now);
        }
        break;

    case APP_MODE_CALL_RECEIVED:
        if (event->button == BADGE_BUTTON_ACTION) {
            if (event->type == BADGE_BUTTON_EVENT_SHORT) {
                s_app.active_peer_id = s_app.pending_peer_id;
                send_control(s_app.active_peer_id, BADGE_PACKET_CALL_ACCEPT,
                             s_app.my_id);
                enter_mode(APP_MODE_CALL_ACTIVE, now);
            } else if (event->type == BADGE_BUTTON_EVENT_LONG) {
                send_control(s_app.pending_peer_id, BADGE_PACKET_CALL_REJECT,
                             s_app.my_id);
                enter_mode(APP_MODE_MAIN, now);
            }
        }
        break;

    case APP_MODE_OUTGOING_WAIT:
        if (event->type == BADGE_BUTTON_EVENT_LONG &&
            event->button == BADGE_BUTTON_ACTION) {
            end_call(now, "action_long_cancel");
        }
        break;

    case APP_MODE_CALL_ACTIVE:
        if (event->button == BADGE_BUTTON_ACTION) {
            if (event->type == BADGE_BUTTON_EVENT_PRESS) {
                s_app.action_pressed = true;
                s_app.action_press_started_ms = now;
                refresh_call_activity(now);
                badge_display_set_send_led(true);
                send_reliable_input(s_app.active_peer_id,
                                    BADGE_PACKET_CALL_INPUT_RAW,
                                    CALL_INPUT_RAW_ARG_START, 0, false, NULL);
            } else if (event->type == BADGE_BUTTON_EVENT_RELEASE &&
                       s_app.action_pressed) {
                uint32_t duration = event->duration_ms;
                if (duration > RAW_DURATION_MAX_MS) {
                    duration = RAW_DURATION_MAX_MS;
                }
                s_app.action_pressed = false;
                badge_display_set_send_led(false);
                refresh_call_activity(now);
                cancel_reliable_packet(s_app.active_peer_id,
                                       BADGE_PACKET_CALL_INPUT_RAW,
                                       CALL_INPUT_RAW_ARG_START);
                send_reliable_input(s_app.active_peer_id,
                                    BADGE_PACKET_CALL_INPUT_RAW,
                                    CALL_INPUT_RAW_ARG_STOP, (uint16_t)duration,
                                    false, NULL);
            }
        } else if (event->type == BADGE_BUTTON_EVENT_SHORT &&
                   event->button == BADGE_BUTTON_DOWN) {
            refresh_call_activity(now);
            (void)enqueue_symbol(BADGE_PACKET_CALL_INPUT_SHORT);
        } else if (event->type == BADGE_BUTTON_EVENT_SHORT &&
                   event->button == BADGE_BUTTON_UP) {
            refresh_call_activity(now);
            (void)enqueue_symbol(BADGE_PACKET_CALL_INPUT_LONG);
        }
        break;

    default:
        break;
    }
}

static void update_timeouts(uint32_t now)
{
    if (s_app.mode == APP_MODE_ID_PROBE_WAIT &&
        deadline_reached(now, s_app.probe_deadline_ms)) {
        if (!s_app.probe_conflict_seen) {
            accept_id(s_app.probe_candidate_id, now);
            return;
        }

        badge_display_play_error_dance();
        if (s_app.auto_assigning) {
            if (deadline_reached(now, s_app.next_auto_retry_ms)) {
                s_app.next_auto_retry_ms = now + ID_AUTO_RETRY_MS;
                start_id_probe(random_badge_id(), true, now);
            }
        } else {
            enter_mode(APP_MODE_MY_ID_INPUT, now);
        }
    }

    if (s_app.mode == APP_MODE_CALL_RECEIVED &&
        (now - s_app.mode_started_ms) >= INCOMING_CALL_TIMEOUT_MS) {
        send_control(s_app.pending_peer_id, BADGE_PACKET_CALL_REJECT, s_app.my_id);
        enter_mode(APP_MODE_MAIN, now);
    }

    if (s_app.mode == APP_MODE_OUTGOING_WAIT &&
        (now - s_app.mode_started_ms) >= OUTGOING_CALL_TIMEOUT_MS) {
        badge_display_play_error_dance();
        end_call(now, "outgoing_timeout");
    }

    if (s_app.mode == APP_MODE_CALL_ACTIVE &&
        s_app.call_deadline_ms != 0U &&
        deadline_reached(now, s_app.call_deadline_ms)) {
        ESP_LOGI(TAG, "active call timeout: now=%lu last_activity=%lu deadline=%lu",
                 (unsigned long)now, (unsigned long)s_app.last_activity_ms,
                 (unsigned long)s_app.call_deadline_ms);
        end_call(now, "inactivity_timeout");
    }

    if (s_app.send_symbol_until_ms != 0U &&
        deadline_reached(now, s_app.send_symbol_until_ms)) {
        s_app.send_symbol_until_ms = 0;
        s_app.next_symbol_start_ms = now + SYMBOL_GAP_MS;
        if (!s_app.action_pressed && !s_app.call_alert_on) {
            badge_display_set_send_led(false);
        }
    }

    if (s_app.receive_raw_until_ms != 0U &&
        deadline_reached(now, s_app.receive_raw_until_ms)) {
        s_app.receive_raw_until_ms = 0;
        if (!s_app.call_alert_on) {
            badge_display_set_receive_led(false);
        }
    }
}

static void update_alerts(uint32_t now)
{
    if (s_app.mode != APP_MODE_CALL_RECEIVED) {
        return;
    }

    if ((now - s_app.last_alert_toggle_ms) < ALERT_BLINK_MS) {
        return;
    }

    s_app.last_alert_toggle_ms = now;
    s_app.call_alert_on = !s_app.call_alert_on;
    badge_display_set_send_led(s_app.call_alert_on);
    badge_display_set_receive_led(s_app.call_alert_on);
}

static void update_input_repeat(uint32_t now)
{
    if (!s_app.input_repeat_active) {
        return;
    }

    if (!mode_supports_input_repeat(s_app.mode)) {
        stop_input_repeat();
        return;
    }

    if (deadline_reached(now, s_app.next_input_repeat_ms)) {
        repeat_input_step(now);
    }
}

static void update_reliable_retries(uint32_t now)
{
    for (uint8_t i = 0; i < RELIABLE_PACKET_COUNT; i++) {
        reliable_packet_t *slot = &s_app.reliable_packets[i];
        if (!slot->active || !deadline_reached(now, slot->next_retry_ms)) {
            continue;
        }

        if (slot->attempts >= RELIABLE_MAX_ATTEMPTS) {
            ESP_LOGW(TAG, "reliable packet dropped: type=%s dst=%u seq=%u",
                     badge_packet_type_name(slot->packet.type),
                     (unsigned)slot->packet.dst_id, (unsigned)slot->packet.seq);
            if (s_app.queued_symbol_waiting_ack &&
                s_app.queued_symbol_seq == slot->packet.seq) {
                s_app.queued_symbol_waiting_ack = false;
                s_app.queued_symbol_seq = 0;
            }
            slot->active = false;
            continue;
        }

        esp_err_t err = badge_comms_send_packet(&slot->packet);
        if (err == ESP_OK) {
            slot->attempts++;
            slot->next_retry_ms = now + RELIABLE_RETRY_MS;
            ESP_LOGI(TAG, "retry %s dst=%u seq=%u attempt=%u",
                     badge_packet_type_name(slot->packet.type),
                     (unsigned)slot->packet.dst_id, (unsigned)slot->packet.seq,
                     (unsigned)slot->attempts);
        } else {
            ESP_LOGW(TAG, "retry failed %s dst=%u seq=%u",
                     badge_packet_type_name(slot->packet.type),
                     (unsigned)slot->packet.dst_id, (unsigned)slot->packet.seq);
            slot->next_retry_ms = now + RELIABLE_RETRY_MS;
        }
    }
}

static void update_symbol_queue(uint32_t now)
{
    if (s_app.mode != APP_MODE_CALL_ACTIVE ||
        s_app.action_pressed ||
        s_app.queued_symbol_waiting_ack ||
        s_app.send_symbol_until_ms != 0U ||
        s_app.symbol_queue_count == 0U) {
        return;
    }

    if (s_app.next_symbol_start_ms != 0U &&
        !deadline_reached(now, s_app.next_symbol_start_ms)) {
        return;
    }

    queued_symbol_t symbol;
    if (!dequeue_symbol(&symbol)) {
        return;
    }

    refresh_call_activity(now);
    show_send_symbol(now, symbol.duration_ms);
    uint16_t seq = 0;
    send_reliable_input(s_app.active_peer_id, symbol.type, 0,
                        symbol.duration_ms, false, &seq);
    if (seq != 0U) {
        s_app.queued_symbol_waiting_ack = true;
        s_app.queued_symbol_seq = seq;
    }
    ESP_LOGI(TAG, "sent queued %s seq=%u remaining=%u",
             badge_packet_type_name(symbol.type), (unsigned)seq,
             (unsigned)s_app.symbol_queue_count);
}

void app_main(void)
{
    ESP_ERROR_CHECK(badge_storage_init());
    ESP_ERROR_CHECK(badge_display_init());
    ESP_ERROR_CHECK(badge_buttons_init());
    ESP_ERROR_CHECK(badge_comms_init());
    ESP_ERROR_CHECK(badge_storage_load_my_id(&s_app.my_id));

    s_app.selected_my_id = s_app.my_id;
    s_app.target_id = 1U;

    ESP_LOGI(TAG, "Fallout badge firmware");
    ESP_LOGI(TAG, "Using pinout: %s", badge_hardware_pinout_name());
    ESP_LOGI(TAG,
             "GPIO map: MID={%u,%u,%u} SID={%u,%u,%u} SLED=%u RLED=%u "
             "buttons={up:%u,down:%u,action:%u}",
             (unsigned)BADGE_PINS.my_id[0], (unsigned)BADGE_PINS.my_id[1],
             (unsigned)BADGE_PINS.my_id[2], (unsigned)BADGE_PINS.send_id[0],
             (unsigned)BADGE_PINS.send_id[1], (unsigned)BADGE_PINS.send_id[2],
             (unsigned)BADGE_PINS.send_led, (unsigned)BADGE_PINS.receive_led,
             (unsigned)BADGE_PINS.button_up, (unsigned)BADGE_PINS.button_down,
             (unsigned)BADGE_PINS.button_action);

    const uint32_t boot_ms = now_ms();
    if (s_app.my_id == 0U) {
        ESP_LOGI(TAG, "No saved badge ID; entering ID input");
        enter_mode(APP_MODE_MY_ID_INPUT, boot_ms);
    } else {
        ESP_LOGI(TAG, "Loaded badge ID %u", (unsigned)s_app.my_id);
        enter_mode(APP_MODE_MAIN, boot_ms);
    }

    while (true) {
        const uint32_t now = now_ms();

        badge_buttons_poll(now);

        badge_button_event_t button_event;
        while (badge_buttons_pop_event(&button_event)) {
            handle_button_event(&button_event, now);
        }

        badge_packet_t packet;
        while (badge_comms_pop_received(&packet)) {
            handle_packet(&packet, now);
        }

        update_timeouts(now);
        update_alerts(now);
        update_input_repeat(now);
        update_reliable_retries(now);
        update_symbol_queue(now);

        vTaskDelay(delay_ticks_at_least_one(MAIN_LOOP_MS));
    }
}
