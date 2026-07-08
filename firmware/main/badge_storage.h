#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t badge_storage_init(void);
esp_err_t badge_storage_load_my_id(uint8_t *id);
esp_err_t badge_storage_save_my_id(uint8_t id);

#ifdef __cplusplus
}
#endif
