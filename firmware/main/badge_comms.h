#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BADGE_BROADCAST_ID 0U
#define BADGE_PROTOCOL_VERSION 1U

typedef enum {
    BADGE_PACKET_ID_PROBE = 1,
    BADGE_PACKET_ID_PROBE_ACK,
    BADGE_PACKET_ID_CLAIM,
    BADGE_PACKET_ID_CONFLICT,
    BADGE_PACKET_CALL_REQUEST,
    BADGE_PACKET_CALL_ACCEPT,
    BADGE_PACKET_CALL_REJECT,
    BADGE_PACKET_CALL_HOLD,
    BADGE_PACKET_CALL_END,
    BADGE_PACKET_CALL_INPUT_RAW,
    BADGE_PACKET_CALL_INPUT_SHORT,
    BADGE_PACKET_CALL_INPUT_LONG,
    BADGE_PACKET_PING,
} badge_packet_type_t;

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t version;
    uint8_t type;
    uint8_t src_id;
    uint8_t dst_id;
    uint16_t seq;
    uint8_t arg;
    uint16_t duration_ms;
} badge_packet_t;

esp_err_t badge_comms_init(void);
esp_err_t badge_comms_send(uint8_t src_id, uint8_t dst_id,
                           badge_packet_type_t type, uint8_t arg,
                           uint16_t duration_ms);
esp_err_t badge_comms_send_control(uint8_t src_id, uint8_t dst_id,
                                   badge_packet_type_t type, uint8_t arg);
bool badge_comms_pop_received(badge_packet_t *packet);
const char *badge_packet_type_name(uint8_t type);

#ifdef __cplusplus
}
#endif
