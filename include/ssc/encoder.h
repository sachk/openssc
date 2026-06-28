#ifndef SSCENC_ENCODER_H
#define SSCENC_ENCODER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* S948BXXS3AZF4 phone captures send one 484-byte SSC frame per RTP packet at
 * 192 kbps basic SSC. The Samsung blob returns that size for 864 PCM frames;
 * 480-frame calls produce shorter packets that do not match the phone path. */
#define SSCENC_FRAME_SAMPLES 864u
#define SSCENC_MAX_CHANNELS 2u

typedef enum {
    SSCENC_OK = 0,
    SSCENC_EINVAL = -1,
    SSCENC_ENOSPC = -2,
    SSCENC_ENOMEM = -3
} sscenc_status;

typedef struct {
    uint32_t sample_rate;
    uint32_t bitrate;
    uint8_t channels;
    uint8_t bits_per_sample;
} sscenc_config;

typedef struct sscenc_encoder sscenc_encoder;

int sscenc_config_basic(sscenc_config *cfg, uint32_t sample_rate, uint8_t channels, uint32_t bitrate);
int sscenc_header_for_bitrate(uint32_t bitrate, uint8_t *header);
size_t sscenc_frame_bound(const sscenc_config *cfg, size_t frame_samples);

sscenc_encoder *sscenc_create(const sscenc_config *cfg);
void sscenc_destroy(sscenc_encoder *enc);
void sscenc_reset(sscenc_encoder *enc);

int sscenc_encode_s16(sscenc_encoder *enc,
                      const int16_t *pcm,
                      size_t frame_samples,
                      uint8_t *out,
                      size_t out_cap,
                      size_t *written);
int sscenc_encode_s32(sscenc_encoder *enc,
                      const int32_t *pcm,
                      size_t frame_samples,
                      uint8_t *out,
                      size_t out_cap,
                      size_t *written);

#ifdef __cplusplus
}
#endif

#endif
