#include <peer.h>

#define LOG_TAG "realtimeapi-sdk"
#define MAX_HTTP_OUTPUT_BUFFER 4096

void oai_wifi(void);
void oai_init_audio_capture(void);
void oai_init_audio_decoder(void);
void oai_init_audio_encoder();
void oai_send_audio(PeerConnection *peer_connection);
void oai_audio_decode(uint8_t *data, size_t size);
void oai_dump_audio_buffer(void);
void oai_check_audio_timeout(void);
void oai_webrtc();
bool oai_http_request(char *offer, char *answer);
