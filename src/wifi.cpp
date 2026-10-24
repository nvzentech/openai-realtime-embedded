#include <assert.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_sntp.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"

static bool g_wifi_connected = false;

static void oai_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data) {
  static int s_retry_num = 0;

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
      wifi_event_sta_disconnected_t *event =
          (wifi_event_sta_disconnected_t *)event_data;

      g_wifi_connected = false;
      ESP_LOGE(LOG_TAG, "WiFi disconnected, reason=%d", event->reason);

      if (s_retry_num < 5) {
          esp_wifi_connect();
          s_retry_num++;
          ESP_LOGI(LOG_TAG, "Retrying WiFi connection (%d/5)", s_retry_num);
      } else {
          ESP_LOGE(LOG_TAG, "WiFi connection failed after 5 retries");
      }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
      ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
      ESP_LOGI(LOG_TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
      s_retry_num = 0;
      g_wifi_connected = true;
  }
}

static void sync_time_sntp(void) {
  ESP_LOGI(LOG_TAG, "Initializing SNTP for real-time clock synchronization...");
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  int retry = 0;
  const int retry_count = 10;
  while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
    ESP_LOGD(LOG_TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  time_t now = 0;
  struct tm timeinfo = { 0 };
  time(&now);
  localtime_r(&now, &timeinfo);
  ESP_LOGI(LOG_TAG, "Current time: %s", asctime(&timeinfo));
}

void oai_wifi(void) {
  ESP_ERROR_CHECK(esp_netif_init());

  esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
  assert(sta_netif);

  ESP_ERROR_CHECK(esp_event_handler_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &oai_event_handler, NULL));

  ESP_ERROR_CHECK(esp_event_handler_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &oai_event_handler, NULL));

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

  wifi_config_t wifi_config = {};
  strncpy((char *)wifi_config.sta.ssid, WIFI_SSID,
          sizeof(wifi_config.sta.ssid) - 1);
  strncpy((char *)wifi_config.sta.password, WIFI_PASSWORD,
          sizeof(wifi_config.sta.password) - 1);
  wifi_config.sta.pmf_cfg.capable = true;
  wifi_config.sta.pmf_cfg.required = false;

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(LOG_TAG, "Connecting to WiFi SSID: %s", WIFI_SSID);
  esp_err_t err = esp_wifi_connect();
  if (err != ESP_OK) {
    ESP_LOGE(LOG_TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
  }

  // Block until we get an IP address
  while (!g_wifi_connected) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  ESP_LOGI(LOG_TAG, "WiFi connected successfully to SSID: %s", WIFI_SSID);
  sync_time_sntp();
}
