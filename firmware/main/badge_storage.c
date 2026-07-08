#include "badge_storage.h"

#include "esp_check.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "badge_storage";
static const char *NVS_NAMESPACE = "fallout";
static const char *NVS_KEY_MY_ID = "my_id";

esp_err_t badge_storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase failed");
        err = nvs_flash_init();
    }

    return err;
}

esp_err_t badge_storage_load_my_id(uint8_t *id)
{
    if (id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *id = 0;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "NVS open failed");

    uint8_t stored_id = 0;
    err = nvs_get_u8(handle, NVS_KEY_MY_ID, &stored_id);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "NVS read failed");

    if (stored_id <= 63U) {
        *id = stored_id;
    }

    return ESP_OK;
}

esp_err_t badge_storage_save_my_id(uint8_t id)
{
    if (id > 63U) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle),
                        TAG, "NVS open failed");

    esp_err_t err = nvs_set_u8(handle, NVS_KEY_MY_ID, id);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    return err;
}
