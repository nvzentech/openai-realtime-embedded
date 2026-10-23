#include <assert.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"

static bool g_wifi_connected = false;

//for testing we add this
// static void scan_wifi_networks(void) {
//     wifi_scan_config_t scan_config = {};
//     ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

//     uint16_t count = 0;
//     ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&count));

//     wifi_ap_record_t *aps = (wifi_ap_record_t *)
//         calloc(count ? count : 1, sizeof(wifi_ap_record_t));
//     if (aps == NULL) {
//         ESP_LOGE(LOG_TAG, "Failed to allocate scan results");
//         return;
//     }

//     ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&count, aps));

//     ESP_LOGI(LOG_TAG, "WiFi scan found %u networks", count);
//     for (int i = 0; i < count; i++) {
//         ESP_LOGI(LOG_TAG, "SSID: %s, RSSI: %d, channel: %d, auth: %d",
//                  (char *)aps[i].ssid,
//                  aps[i].rssi,
//                  aps[i].primary,
//                  aps[i].authmode);
//     }

//     free(aps);
// }


static void oai_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data) {
  static int s_retry_num = 0;

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
      wifi_event_sta_disconnected_t *event =
          (wifi_event_sta_disconnected_t *)event_data;

      g_wifi_connected = false;

      //scan_wifi_networks(); //for testing we add this

      ESP_LOGE(LOG_TAG, "WiFi disconnected, reason=%d",
              event->reason);

      if (s_retry_num < 5) {
          esp_wifi_connect();
          s_retry_num++;
          ESP_LOGI(LOG_TAG, "Retrying WiFi connection (%d/5)",
                  s_retry_num);
      } else {
          ESP_LOGE(LOG_TAG,
                  "WiFi connection failed after 5 retries");
      }
  } else if (event_base == IP_EVENT &&
            event_id == IP_EVENT_STA_GOT_IP) {
      ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

      ESP_LOGI(LOG_TAG, "Got IP: " IPSTR,
              IP2STR(&event->ip_info.ip));

      s_retry_num = 0;
      g_wifi_connected = true;
  }

}

void oai_wifi(void) {
// Initialize the network interface and default event loop
// only if they have not already been initialized elsewhere.
ESP_ERROR_CHECK(esp_netif_init());
ESP_ERROR_CHECK(esp_event_loop_create_default());

esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
assert(sta_netif);

// Register handlers after the default event loop exists.
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

/*
  pmf is protected memory frames:
  it help to provide security to the wifi network,
  Modern mobile hotspots (Android & iOS) mandate PMF (802.11w)
  support. Without it, the phone ignores association requests.
  capable = true : if true, the device will support PMF
*/

// Apply credentials before starting Wi-Fi.
ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
ESP_ERROR_CHECK(esp_wifi_start());

// Optional: scan after starting Wi-Fi, before connecting.

//scan_wifi_networks(); // for testing we add this
ESP_LOGE(LOG_TAG,"WiFi connecting to SSID: %s", WIFI_SSID);// for testing we add this
ESP_LOGE(LOG_TAG,"WiFi connecting with password: %s", WIFI_PASSWORD);// for testing we add this
esp_err_t err = 0; 
ESP_ERROR_CHECK(err= esp_wifi_connect());
ESP_LOGE(LOG_TAG,"WiFi connect returned: %d", err);

  // block until we get an IP address
  while (!g_wifi_connected) {
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}
