#include "esp_log.h"
#include "focusdesk_common.h"
#include "app_tasks.h"

static const char *TAG = "focusdesk";

void app_main(void)
{
    ESP_LOGI(TAG, "FocusDesk Hello (common v%d)", FOCUSDESK_COMMON_VERSION);

    fd_err_t err = app_tasks_start();
    if (err != FD_OK) {
        ESP_LOGE(TAG, "app_tasks_start failed: %d", (int)err);
    }
}
