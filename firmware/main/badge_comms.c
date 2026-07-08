#include "badge_comms.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "badge_comms";
static const uint16_t PACKET_MAGIC = 0xbad6U;
static const uint8_t BROADCAST_MAC[ESP_NOW_ETH_ALEN] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};
static const uint8_t CONTROL_REPEAT_COUNT = 3U;

static QueueHandle_t s_rx_queue;
static uint16_t s_next_seq = 1;

static esp_err_t send_packet(const badge_packet_t *packet)
{
    return esp_now_send(BROADCAST_MAC, (const uint8_t *)packet, sizeof(*packet));
}

static void receive_callback(const esp_now_recv_info_t *recv_info,
                             const uint8_t *data, int len)
{
    (void)recv_info;

    if (data == NULL || len != sizeof(badge_packet_t) || s_rx_queue == NULL) {
        return;
    }

    badge_packet_t packet;
    memcpy(&packet, data, sizeof(packet));

    if (packet.magic != PACKET_MAGIC ||
        packet.version != BADGE_PROTOCOL_VERSION ||
        packet.src_id > 63U ||
        packet.dst_id > 63U) {
        return;
    }

    (void)xQueueSend(s_rx_queue, &packet, 0);
}

esp_err_t badge_comms_init(void)
{
    s_rx_queue = xQueueCreate(16, sizeof(badge_packet_t));
    if (s_rx_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(err, TAG, "event loop create failed");
    }

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_config), TAG, "Wi-Fi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG,
                        "Wi-Fi storage config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG,
                        "Wi-Fi station mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG,
                        "Wi-Fi power-save config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE), TAG,
                        "Wi-Fi channel config failed");

    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "ESP-NOW init failed");
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(receive_callback), TAG,
                        "ESP-NOW receive callback failed");

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, BROADCAST_MAC, sizeof(BROADCAST_MAC));
    peer.channel = 1;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        ESP_RETURN_ON_ERROR(err, TAG, "broadcast peer add failed");
    }

    ESP_LOGI(TAG, "ESP-NOW initialized on channel 1");
    return ESP_OK;
}

esp_err_t badge_comms_send(uint8_t src_id, uint8_t dst_id,
                           badge_packet_type_t type, uint8_t arg,
                           uint16_t duration_ms)
{
    badge_packet_t packet;
    ESP_RETURN_ON_ERROR(badge_comms_make_packet(src_id, dst_id, type, arg,
                                                duration_ms, &packet),
                        TAG, "packet create failed");
    return badge_comms_send_packet(&packet);
}

esp_err_t badge_comms_make_packet(uint8_t src_id, uint8_t dst_id,
                                  badge_packet_type_t type, uint8_t arg,
                                  uint16_t duration_ms,
                                  badge_packet_t *packet)
{
    if (src_id > 63U || dst_id > 63U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (packet == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *packet = (badge_packet_t){
        .magic = PACKET_MAGIC,
        .version = BADGE_PROTOCOL_VERSION,
        .type = (uint8_t)type,
        .src_id = src_id,
        .dst_id = dst_id,
        .seq = s_next_seq++,
        .arg = arg,
        .duration_ms = duration_ms,
    };

    return ESP_OK;
}

esp_err_t badge_comms_send_packet(const badge_packet_t *packet)
{
    if (packet == NULL ||
        packet->magic != PACKET_MAGIC ||
        packet->version != BADGE_PROTOCOL_VERSION ||
        packet->src_id > 63U ||
        packet->dst_id > 63U) {
        return ESP_ERR_INVALID_ARG;
    }

    return send_packet(packet);
}

esp_err_t badge_comms_send_control(uint8_t src_id, uint8_t dst_id,
                                   badge_packet_type_t type, uint8_t arg)
{
    esp_err_t last_err = ESP_OK;

    if (src_id > 63U || dst_id > 63U) {
        return ESP_ERR_INVALID_ARG;
    }

    const badge_packet_t packet = {
        .magic = PACKET_MAGIC,
        .version = BADGE_PROTOCOL_VERSION,
        .type = (uint8_t)type,
        .src_id = src_id,
        .dst_id = dst_id,
        .seq = s_next_seq++,
        .arg = arg,
        .duration_ms = 0,
    };

    for (uint8_t i = 0; i < CONTROL_REPEAT_COUNT; i++) {
        last_err = send_packet(&packet);
        if (last_err != ESP_OK) {
            return last_err;
        }
    }

    return last_err;
}

bool badge_comms_pop_received(badge_packet_t *packet)
{
    if (packet == NULL || s_rx_queue == NULL) {
        return false;
    }

    return xQueueReceive(s_rx_queue, packet, 0) == pdTRUE;
}

const char *badge_packet_type_name(uint8_t type)
{
    switch ((badge_packet_type_t)type) {
    case BADGE_PACKET_ID_PROBE:
        return "ID_PROBE";
    case BADGE_PACKET_ID_PROBE_ACK:
        return "ID_PROBE_ACK";
    case BADGE_PACKET_ID_CLAIM:
        return "ID_CLAIM";
    case BADGE_PACKET_ID_CONFLICT:
        return "ID_CONFLICT";
    case BADGE_PACKET_CALL_REQUEST:
        return "CALL_REQUEST";
    case BADGE_PACKET_CALL_ACCEPT:
        return "CALL_ACCEPT";
    case BADGE_PACKET_CALL_REJECT:
        return "CALL_REJECT";
    case BADGE_PACKET_CALL_HOLD:
        return "CALL_HOLD";
    case BADGE_PACKET_CALL_END:
        return "CALL_END";
    case BADGE_PACKET_CALL_INPUT_RAW:
        return "CALL_INPUT_RAW";
    case BADGE_PACKET_CALL_INPUT_SHORT:
        return "CALL_INPUT_SHORT";
    case BADGE_PACKET_CALL_INPUT_LONG:
        return "CALL_INPUT_LONG";
    case BADGE_PACKET_PING:
        return "PING";
    case BADGE_PACKET_ACK:
        return "ACK";
    default:
        return "UNKNOWN";
    }
}
