#include "ssc/encoder.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t b[2]) { return (uint16_t)(b[0] | ((uint16_t)b[1] << 8)); }
static uint32_t rd32(const uint8_t b[4]) { return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); }

static int read_exact(FILE *f, void *p, size_t n)
{
    return fread(p, 1, n, f) == n ? 0 : -1;
}

static int skip_bytes(FILE *f, uint32_t n)
{
    if (fseek(f, (long)(n + (n & 1u)), SEEK_CUR) == 0)
        return 0;
    return -1;
}

static int read_wav_header(FILE *f, uint32_t *rate, uint16_t *channels, uint16_t *bits, uint32_t *data_size)
{
    uint8_t h[12];
    int have_fmt = 0;

    if (read_exact(f, h, sizeof(h)) || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))
        return -1;

    for (;;) {
        uint8_t ch[8];
        if (read_exact(f, ch, sizeof(ch)))
            return -1;
        uint32_t size = rd32(ch + 4);
        if (!memcmp(ch, "fmt ", 4)) {
            uint8_t fmt[40];
            size_t n = size < sizeof(fmt) ? size : sizeof(fmt);
            if (read_exact(f, fmt, n))
                return -1;
            if (size > n && skip_bytes(f, size - (uint32_t)n))
                return -1;
            if (size < 16 || rd16(fmt) != 1)
                return -1;
            *channels = rd16(fmt + 2);
            *rate = rd32(fmt + 4);
            *bits = rd16(fmt + 14);
            have_fmt = 1;
        } else if (!memcmp(ch, "data", 4)) {
            if (!have_fmt)
                return -1;
            *data_size = size;
            return 0;
        } else if (skip_bytes(f, size)) {
            return -1;
        }
    }
}

static int parse_bitrate(const char *s, uint32_t *out)
{
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno || !end || *end || v > UINT32_MAX)
        return -1;
    *out = (uint32_t)v;
    return 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr, "usage: %s [-b 88000|96000|128000|192000|229000] input.wav output.ssc\n", argv0);
}

int main(int argc, char **argv)
{
    uint32_t bitrate = 192000;
    const char *in_path;
    const char *out_path;
    FILE *in = NULL;
    FILE *out = NULL;
    sscenc_config cfg;
    sscenc_encoder *enc = NULL;
    uint32_t rate = 0, data_size = 0;
    uint16_t channels = 0, bits = 0;
    int16_t pcm[SSCENC_FRAME_SAMPLES * SSCENC_MAX_CHANNELS];
    uint8_t frame[512];
    size_t frames_written = 0;
    int rc = 1;

    if (argc == 5 && strcmp(argv[1], "-b") == 0) {
        if (parse_bitrate(argv[2], &bitrate)) {
            usage(argv[0]);
            return 2;
        }
        in_path = argv[3];
        out_path = argv[4];
    } else if (argc == 3) {
        in_path = argv[1];
        out_path = argv[2];
    } else {
        usage(argv[0]);
        return 2;
    }

    in = fopen(in_path, "rb");
    if (!in) {
        perror(in_path);
        goto done;
    }
    if (read_wav_header(in, &rate, &channels, &bits, &data_size) || bits != 16 || channels == 0 || channels > SSCENC_MAX_CHANNELS) {
        fprintf(stderr, "unsupported wav: need 16-bit PCM mono/stereo 44.1/48 kHz\n");
        goto done;
    }
    if (sscenc_config_basic(&cfg, rate, (uint8_t)channels, bitrate) != SSCENC_OK) {
        fprintf(stderr, "unsupported encoder config: rate=%u channels=%u bitrate=%u\n", rate, channels, bitrate);
        goto done;
    }
    if (sscenc_frame_bound(&cfg, SSCENC_FRAME_SAMPLES) > sizeof(frame)) {
        fprintf(stderr, "internal frame buffer too small\n");
        goto done;
    }

    enc = sscenc_create(&cfg);
    if (!enc) {
        fprintf(stderr, "encoder allocation failed\n");
        goto done;
    }
    out = fopen(out_path, "wb");
    if (!out) {
        perror(out_path);
        goto done;
    }

    const size_t samples_per_frame = SSCENC_FRAME_SAMPLES * channels;
    size_t samples_left = data_size / sizeof(int16_t);
    while (samples_left) {
        size_t want = samples_left < samples_per_frame ? samples_left : samples_per_frame;
        memset(pcm, 0, sizeof(pcm));
        if (fread(pcm, sizeof(int16_t), want, in) != want) {
            fprintf(stderr, "short read\n");
            goto done;
        }
        size_t written = 0;
        if (sscenc_encode_s16(enc, pcm, SSCENC_FRAME_SAMPLES, frame, sizeof(frame), &written) != SSCENC_OK) {
            fprintf(stderr, "encode failed\n");
            goto done;
        }
        if (fwrite(frame, 1, written, out) != written) {
            fprintf(stderr, "write failed\n");
            goto done;
        }
        samples_left -= want;
        ++frames_written;
    }

    fprintf(stderr, "encoded %zu frame%s\n", frames_written, frames_written == 1 ? "" : "s");
    rc = 0;

done:
    sscenc_destroy(enc);
    if (out)
        fclose(out);
    if (in)
        fclose(in);
    return rc;
}
