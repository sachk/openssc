#include "ssc/encoder.h"

#include <stdlib.h>
#include <string.h>

struct sscenc_encoder {
    sscenc_config cfg;
    uint16_t seq;
    uint8_t flags;
    int32_t pred[SSCENC_MAX_CHANNELS];
};

static int basic_bitrate(uint32_t bitrate)
{
    switch (bitrate) {
    case 88000:
    case 96000:
    case 128000:
    case 192000:
    case 229000:
        return 1;
    default:
        return 0;
    }
}

int sscenc_header_for_bitrate(uint32_t bitrate, uint8_t *header)
{
    uint8_t h;

    switch (bitrate) {
    case 88000:  h = 0xf0; break;
    case 96000:  h = 0xf1; break;
    case 128000: h = 0xf2; break;
    case 192000: h = 0xf3; break;
    case 229000: h = 0xf4; break;
    default:
        return SSCENC_EINVAL;
    }

    if (header)
        *header = h;
    return SSCENC_OK;
}

int sscenc_config_basic(sscenc_config *cfg, uint32_t sample_rate, uint8_t channels, uint32_t bitrate)
{
    if (!cfg || (sample_rate != 44100 && sample_rate != 48000) || channels == 0 || channels > SSCENC_MAX_CHANNELS || !basic_bitrate(bitrate))
        return SSCENC_EINVAL;

    cfg->sample_rate = sample_rate;
    cfg->bitrate = bitrate;
    cfg->channels = channels;
    cfg->bits_per_sample = 16;
    return SSCENC_OK;
}

static uint32_t budget_rate(uint32_t sample_rate)
{
    return sample_rate == 48000 ? 44100u : sample_rate;
}

size_t sscenc_frame_bound(const sscenc_config *cfg, size_t frame_samples)
{
    uint64_t num;
    size_t payload;
    uint32_t rate;

    if (!cfg || frame_samples == 0)
        return 0;

    rate = budget_rate(cfg->sample_rate);
    if (rate == 44100 && frame_samples == SSCENC_FRAME_SAMPLES) {
        switch (cfg->bitrate) {
        case 88000:  return 132;
        case 96000:  return 146;
        case 128000: return 192;
        case 192000: return 288;
        case 229000: return 342;
        default: break;
        }
    }

    num = (uint64_t)cfg->bitrate * frame_samples;
    payload = (size_t)((num + (uint64_t)rate * 8u - 1u) / ((uint64_t)rate * 8u));
    payload = (payload + 5u) & ~(size_t)1u;
    if (payload < 8)
        payload = 8;
    return payload + 4u;
}

sscenc_encoder *sscenc_create(const sscenc_config *cfg)
{
    sscenc_encoder *enc;

    if (!cfg || cfg->bits_per_sample != 16 || (cfg->sample_rate != 44100 && cfg->sample_rate != 48000) || cfg->channels == 0 || cfg->channels > SSCENC_MAX_CHANNELS || !basic_bitrate(cfg->bitrate))
        return NULL;

    enc = (sscenc_encoder *)calloc(1, sizeof(*enc));
    if (!enc)
        return NULL;

    enc->cfg = *cfg;
    enc->flags = 1;
    return enc;
}

void sscenc_destroy(sscenc_encoder *enc)
{
    free(enc);
}

void sscenc_reset(sscenc_encoder *enc)
{
    if (!enc)
        return;
    enc->seq = 0;
    enc->flags = 1;
    enc->pred[0] = 0;
    enc->pred[1] = 0;
}

static void put_nibble(uint8_t *p, size_t idx, uint8_t v)
{
    if (idx & 1u)
        p[idx >> 1] = (uint8_t)(p[idx >> 1] | (v & 0x0f));
    else
        p[idx >> 1] = (uint8_t)((v & 0x0f) << 4);
}

static int32_t clamp_i32(int64_t v, int32_t lo, int32_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return (int32_t)v;
}

static unsigned scale_shift(const int16_t *pcm, size_t samples)
{
    uint32_t peak = 0;

    for (size_t i = 0; i < samples; ++i) {
        int32_t s = pcm[i];
        uint32_t a = (uint32_t)(s < 0 ? -s : s);
        if (a > peak)
            peak = a;
    }

    if (peak == 0)
        return 0;

    unsigned shift = 8;
    while (shift > 0 && (peak >> shift) < 7)
        --shift;
    return shift;
}

static void write_payload(sscenc_encoder *enc, const int16_t *pcm, size_t frames, uint8_t *payload, size_t payload_len)
{
    const size_t channels = enc->cfg.channels;
    const size_t total = frames * channels;
    const size_t body = payload_len > 4 ? payload_len - 4 : 0;
    const size_t symbols = body * 2;
    unsigned shift;
    size_t group;

    memset(payload, 0, payload_len);
    if (payload_len >= 2) {
        payload[0] = 0xff;
        payload[1] = 0xfe;
    }
    if (!pcm || total == 0 || symbols == 0)
        return;

    shift = scale_shift(pcm, total);
    if (shift == 0)
        return;

    payload[2] = (uint8_t)((shift << 4) | channels);
    payload[3] = (uint8_t)(frames >> 1);
    group = (total + symbols - 1u) / symbols;
    if (group == 0)
        group = 1;

    for (size_t sym = 0, pos = 0; sym < symbols && pos < total; ++sym, pos += group) {
        int64_t acc = 0;
        size_t n = group;
        if (pos + n > total)
            n = total - pos;
        for (size_t i = 0; i < n; ++i)
            acc += pcm[pos + i];

        const size_t ch = pos % channels;
        int32_t avg = (int32_t)(acc / (int64_t)n);
        int32_t delta = (avg - enc->pred[ch]) >> shift;
        delta = clamp_i32(delta, -8, 7);
        enc->pred[ch] += delta << shift;
        put_nibble(payload + 4, sym, (uint8_t)(delta & 0x0f));
    }
}

int sscenc_encode_s16(sscenc_encoder *enc,
                      const int16_t *pcm,
                      size_t frame_samples,
                      uint8_t *out,
                      size_t out_cap,
                      size_t *written)
{
    uint8_t hdr;
    size_t frame_len;

    if (written)
        *written = 0;
    if (!enc || !pcm || !out || frame_samples == 0)
        return SSCENC_EINVAL;

    frame_len = sscenc_frame_bound(&enc->cfg, frame_samples);
    if (out_cap < frame_len)
        return SSCENC_ENOSPC;
    if (sscenc_header_for_bitrate(enc->cfg.bitrate, &hdr) != SSCENC_OK)
        return SSCENC_EINVAL;

    out[0] = 0xff;
    out[1] = 0xee;
    out[2] = (uint8_t)(((enc->seq & 0x0f) << 4) | (enc->flags & 0x03));
    out[3] = hdr;

    write_payload(enc, pcm, frame_samples, out + 4, frame_len - 4);

    enc->seq = (uint16_t)((enc->seq + 1u) & 0xffu);
    if (written)
        *written = frame_len;
    return SSCENC_OK;
}
