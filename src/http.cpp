#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_log.h>
#include <string.h>

#include "main.h"

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

esp_err_t oai_http_event_handler(esp_http_client_event_t *evt) {
  static int output_len;
  switch (evt->event_id) {
    case HTTP_EVENT_REDIRECT:
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_REDIRECT");
      esp_http_client_set_redirection(evt->client);
      break;
    case HTTP_EVENT_ERROR:
      ESP_LOGE(LOG_TAG, "HTTP_EVENT_ERROR");
      break;
    case HTTP_EVENT_ON_CONNECTED:
      ESP_LOGI(LOG_TAG, "HTTP_EVENT_ON_CONNECTED to OpenAI");
      break;
    case HTTP_EVENT_HEADER_SENT:
      ESP_LOGI(LOG_TAG, "HTTP_EVENT_HEADER_SENT");
      break;
    case HTTP_EVENT_ON_HEADER:
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_ON_HEADER: %s: %s",
               evt->header_key, evt->header_value);
      break;
    case HTTP_EVENT_ON_DATA: {
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
      if (esp_http_client_is_chunked_response(evt->client)) {
        ESP_LOGE(LOG_TAG, "Chunked HTTP response not supported");
#ifndef LINUX_BUILD
        esp_restart();
#endif
      }

      if (output_len == 0 && evt->user_data) {
        memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
      }

      if (evt->user_data) {
        int copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len - 1));
        if (copy_len > 0) {
          memcpy(((char *)evt->user_data) + output_len, evt->data, copy_len);
          output_len += copy_len;
          ((char *)evt->user_data)[output_len] = '\0';
        }
      }
      break;
    }
    case HTTP_EVENT_ON_FINISH:
      ESP_LOGI(LOG_TAG, "HTTP_EVENT_ON_FINISH (total response len: %d)", output_len);
      output_len = 0;
      break;
    case HTTP_EVENT_DISCONNECTED:
      ESP_LOGI(LOG_TAG, "HTTP_EVENT_DISCONNECTED");
      output_len = 0;
      break;
  }
  return ESP_OK;
}

bool oai_http_request(char *offer, char *answer) {
  esp_http_client_config_t config;
  memset(&config, 0, sizeof(esp_http_client_config_t));

  config.url = OPENAI_REALTIMEAPI;
  config.event_handler = oai_http_event_handler;
  config.user_data = answer;
  config.timeout_ms = 30000;
  config.buffer_size = 4096;
  config.buffer_size_tx = 4096;
  config.crt_bundle_attach = esp_crt_bundle_attach;

  char auth_header[256];
  snprintf(auth_header, sizeof(auth_header), "Bearer %s", OPENAI_API_KEY);

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to initialize HTTP client");
    return false;
  }

  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "application/sdp");
  esp_http_client_set_header(client, "Authorization", auth_header);
  esp_http_client_set_post_field(client, offer, strlen(offer));

  ESP_LOGI(LOG_TAG, "Sending WebRTC SDP offer to OpenAI (%d bytes)...", (int)strlen(offer));

  esp_err_t err = esp_http_client_perform(client);
  int status_code = esp_http_client_get_status_code(client);
  if (err != ESP_OK || status_code != 201) {
    ESP_LOGE(LOG_TAG, "HTTP request failed: err=%s, status=%d, response=%s",
             esp_err_to_name(err), status_code,
             answer ? answer : "null");
    esp_http_client_cleanup(client);
    return false;
  }

  ESP_LOGI(LOG_TAG, "SDP offer accepted by OpenAI (status %d), answer received (%d bytes)",
           status_code, (int)strlen(answer));
  esp_http_client_cleanup(client);
  return true;
}
