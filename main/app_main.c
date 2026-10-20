#include "esp_log.h"
#include "focusdesk_common.h"

static const char *TAG = "focusdesk";

void app_main(void)
{
    ESP_LOGI(TAG, "FocusDesk Hello (common v%d)", FOCUSDESK_COMMON_VERSION);
}