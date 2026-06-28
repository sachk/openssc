#include "ssc/encoder.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_headers(void)
{
    struct row { uint32_t br; uint8_t h; size_t len; } rows[] = {
        { 88000, 0xf0, 224 },
        { 96000, 0xf1, 244 },
        { 128000, 0xf2, 324 },
        { 192000, 0xf3, 484 },
        { 229000, 0xf4, 576 },
    };

    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        sscenc_config cfg;
        uint8_t h = 0;
        assert(sscenc_header_for_bitrate(rows[i].br, &h) == SSCENC_OK);
        assert(h == rows[i].h);
        assert(sscenc_config_basic(&cfg, 44100, 2, rows[i].br) == SSCENC_OK);
        assert(sscenc_frame_bound(&cfg, SSCENC_FRAME_SAMPLES) == rows[i].len);
    }
    assert(sscenc_header_for_bitrate(250000, NULL) == SSCENC_EINVAL);
}

static void test_encode(void)
{
    sscenc_config cfg;
    int16_t silence[SSCENC_FRAME_SAMPLES * 2] = {0};
    int16_t impulse[SSCENC_FRAME_SAMPLES * 2] = {0};
    uint8_t a[640], b[640], c[640];
    size_t an = 0, bn = 0, cn = 0;

    impulse[0] = 16384;
    assert(sscenc_config_basic(&cfg, 44100, 2, 192000) == SSCENC_OK);
    sscenc_encoder *enc = sscenc_create(&cfg);
    assert(enc);

    assert(sscenc_encode_s16(enc, silence, SSCENC_FRAME_SAMPLES, a, sizeof(a), &an) == SSCENC_OK);
    assert(an == 484);
    assert(!memcmp(a, "\xff\xee\x01\xf3\xff\xfe", 6));

    assert(sscenc_encode_s16(enc, silence, SSCENC_FRAME_SAMPLES, b, sizeof(b), &bn) == SSCENC_OK);
    assert(bn == 484);
    assert(b[2] == 0x11);

    sscenc_reset(enc);
    assert(sscenc_encode_s16(enc, impulse, SSCENC_FRAME_SAMPLES, c, sizeof(c), &cn) == SSCENC_OK);
    assert(cn == 484);
    assert(!memcmp(c, "\xff\xee\x01\xf3\xff\xfe", 6));
    assert(memcmp(a + 6, c + 6, an - 6) != 0);

    sscenc_destroy(enc);
}

static void test_validation(void)
{
    sscenc_config cfg;
    assert(sscenc_config_basic(NULL, 44100, 2, 192000) == SSCENC_EINVAL);
    assert(sscenc_config_basic(&cfg, 32000, 2, 192000) == SSCENC_EINVAL);
    assert(sscenc_config_basic(&cfg, 44100, 3, 192000) == SSCENC_EINVAL);
    assert(sscenc_config_basic(&cfg, 44100, 2, 250000) == SSCENC_EINVAL);
}

int main(void)
{
    test_headers();
    test_encode();
    test_validation();
    puts("ok");
    return 0;
}
