/* Experimental Samsung SSC A2DP codec for PipeWire BlueZ5. */

#include <arpa/inet.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <spa/param/audio/format.h>
#include <spa/utils/string.h>

#include "rtp.h"
#include "media-codecs.h"
#include "ssc/encoder.h"

#define SSC_VENDOR_ID 0x00000075u
#define SSC_CODEC_ID  0x0103u

/*
 * S948BXXS3AZF4 libbluetooth_jni.so parses the post-vendor SSC byte as:
 *   0xf0: sample-rate/mode high nibble
 *   0x08: bitrate-limitation flag
 *   0x04: 24-bit / HiFi-ish flag
 *   0x02: UHQ2 / 96 kHz-ish flag
 *
 * The default profile keeps 0x02 off because the recovered working capture used
 * 0x0c. Test profiles deliberately advertise 0x02 and/or unsupported bitrates
 * so Buds behavior can be probed without rebuilding.
 */
#define SSC_CAP_RATE_MASK      0xf0u
#define SSC_CAP_RATE_48000     0x10u
#define SSC_CAP_BITRATE_LIMIT  0x08u
#define SSC_CAP_HIFI_24        0x04u
#define SSC_CAP_UHQ2           0x02u
#define SSC_CAP_BASIC_48K      (SSC_CAP_BITRATE_LIMIT | SSC_CAP_HIFI_24)
#define SSC_CAP_UHQ_TEST       (SSC_CAP_BASIC_48K | SSC_CAP_UHQ2)
#define SSC_CAP_HIFI_OPEN      SSC_CAP_HIFI_24
#define SSC_CAP_UHQ_OPEN       (SSC_CAP_HIFI_24 | SSC_CAP_UHQ2)
#define SSC_PROFILE_ENV        "SSCENC_PROFILE"

struct ssc_profile {
	const char *name;
	uint32_t bitrate;
	uint32_t frame_samples;
	uint8_t capabilities;
};

static const struct ssc_profile ssc_profiles[] = {
	{ "default",       192000,  SSCENC_FRAME_SAMPLES, SSC_CAP_BASIC_48K },
	{ "samsung",       192000,  SSCENC_FRAME_SAMPLES, SSC_CAP_BASIC_48K },
	{ "samsung-basic", 192000,  SSCENC_FRAME_SAMPLES, SSC_CAP_BASIC_48K },
	{ "samsung-default", 192000, SSCENC_FRAME_SAMPLES, SSC_CAP_BASIC_48K },
	{ "basic-88",       88000,  SSCENC_FRAME_SAMPLES, SSC_CAP_BASIC_48K },
	{ "basic-96",       96000,  SSCENC_FRAME_SAMPLES, SSC_CAP_BASIC_48K },
	{ "basic-128",     128000,  SSCENC_FRAME_SAMPLES, SSC_CAP_BASIC_48K },
	{ "basic-192",     192000,  SSCENC_FRAME_SAMPLES, SSC_CAP_BASIC_48K },
	{ "basic-229",     229000,  SSCENC_FRAME_SAMPLES, SSC_CAP_BASIC_48K },
	{ "basic-256",     256000,  SSCENC_FRAME_SAMPLES, SSC_CAP_BASIC_48K },
	{ "basic-328",     328000,  SSCENC_FRAME_SAMPLES, SSC_CAP_BASIC_48K },
	{ "force-high",    328000,  SSCENC_FRAME_SAMPLES, SSC_CAP_BASIC_48K },
	{ "basic-max",     328000,  SSCENC_FRAME_SAMPLES, SSC_CAP_BASIC_48K },
	{ "top-basic",     328000,  SSCENC_FRAME_SAMPLES, SSC_CAP_BASIC_48K },
	{ "open-basic-229", 229000, SSCENC_FRAME_SAMPLES, SSC_CAP_HIFI_OPEN },
	{ "open-basic-256", 256000, SSCENC_FRAME_SAMPLES, SSC_CAP_HIFI_OPEN },
	{ "open-basic-328", 328000, SSCENC_FRAME_SAMPLES, SSC_CAP_HIFI_OPEN },
	{ "uhq-152",       152000,  SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_TEST },
	{ "uhq-250",       250000,  SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_TEST },
	{ "uhq-291",       291000,  SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_TEST },
	{ "uhq-308",       308000,  SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_TEST },
	{ "uhq-442",       442000,  SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_TEST },
	{ "uhq-584",       584000,  SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_TEST },
	{ "uhq-886",       886000,  SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_TEST },
	{ "uhq-max",       886000,  SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_TEST },
	{ "open-uhq-250",  250000,  SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_OPEN },
	{ "open-uhq-291",  291000,  SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_OPEN },
	{ "open-uhq-308",  308000,  SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_OPEN },
	{ "open-uhq-442",  442000,  SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_OPEN },
	{ "short-basic-192", 192000, 512, SSC_CAP_BASIC_48K },
	{ "short-basic-229", 229000, 512, SSC_CAP_BASIC_48K },
	{ "short-uhq-291", 291000, 512, SSC_CAP_UHQ_TEST },
	{ "short-uhq-308", 308000, 512, SSC_CAP_UHQ_TEST },
	{ "short-uhq-442", 442000, 512, SSC_CAP_UHQ_TEST },
	{ "short-uhq-584", 584000, 512, SSC_CAP_UHQ_TEST },
	{ "short-open-uhq-291", 291000, 512, SSC_CAP_UHQ_OPEN },
	{ "short-open-uhq-308", 308000, 512, SSC_CAP_UHQ_OPEN },
	{ "short-open-uhq-442", 442000, 512, SSC_CAP_UHQ_OPEN },
	{ "short-open-uhq-584", 584000, 512, SSC_CAP_UHQ_OPEN },
	{ "stress-512",    512000,  SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_TEST },
	{ "stress-768",    768000,  SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_TEST },
	{ "stress-990",    990000,  SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_TEST },
	{ "stress-1200",  1200000,  SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_TEST },
	{ "stress-1411",  1411000,  SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_TEST },
	{ "stress-2304",  2304000,  SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_TEST },
	{ "probably-broken-2304", 2304000, SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_TEST },
	{ "stress-3200",  3200000,  SSCENC_FRAME_SAMPLES, SSC_CAP_UHQ_TEST },
};

static const struct ssc_profile *ssc_profile_current(void)
{
	const char *name = getenv(SSC_PROFILE_ENV);
	size_t i;

	if (name == NULL || name[0] == '\0')
		name = "default";
	for (i = 0; i < sizeof(ssc_profiles) / sizeof(ssc_profiles[0]); ++i) {
		if (spa_streq(name, ssc_profiles[i].name))
			return &ssc_profiles[i];
	}
	return &ssc_profiles[0];
}

static int ssc_caps_valid_for_profile(uint8_t capabilities, const struct ssc_profile *profile)
{
	if ((capabilities & SSC_CAP_HIFI_24) == 0 ||
	    (capabilities & ~(SSC_CAP_RATE_MASK | SSC_CAP_BITRATE_LIMIT | SSC_CAP_HIFI_24 | SSC_CAP_UHQ2)) != 0)
		return 0;
	if ((capabilities & SSC_CAP_UHQ2) != 0 &&
	    (profile->capabilities & SSC_CAP_UHQ2) == 0)
		return 0;
	return 1;
}
#ifdef SSCENC_BLOB_HELPER
/* Feed the Samsung blob 24-bit samples right-justified in 32-bit containers,
 * matching the negotiated bits_per_sample=24. (If output is silent/quiet,
 * the next thing to try is left-justified S32 — i.e. the blob wanting the
 * 24-bit value in the high bits.) */
#define SSCENC_SPA_FORMAT SPA_AUDIO_FORMAT_S24_32
#define SSCENC_SAMPLE_SIZE sizeof(int32_t)
#else
#define SSCENC_SPA_FORMAT SPA_AUDIO_FORMAT_S16
#define SSCENC_SAMPLE_SIZE sizeof(int16_t)
#endif


struct __attribute__((packed)) a2dp_ssc {
	a2dp_vendor_codec_t info;
	uint8_t capabilities;
};

struct impl {
	sscenc_encoder *enc;
	sscenc_config cfg;
	struct rtp_header *header;
	size_t frame_samples;
	size_t block_size;
};


static int codec_fill_caps(const struct media_codec *codec, uint32_t flags,
		const struct spa_dict *settings, uint8_t caps[A2DP_MAX_CAPS_SIZE])
{
	(void)codec;
	(void)flags;
	(void)settings;
	const struct ssc_profile *profile = ssc_profile_current();
	const struct a2dp_ssc a2dp_ssc = {
		.info.vendor_id = SSC_VENDOR_ID,
		.info.codec_id = SSC_CODEC_ID,
		.capabilities = profile->capabilities,
	};

	memcpy(caps, &a2dp_ssc, sizeof(a2dp_ssc));
	return sizeof(a2dp_ssc);
}

static int codec_select_config(const struct media_codec *codec, uint32_t flags,
		const void *caps, size_t caps_size,
		const struct media_codec_audio_info *info,
		const struct spa_dict *settings, uint8_t config[A2DP_MAX_CAPS_SIZE]
#if SPA_VERSION_BLUEZ5_CODEC_MEDIA >= 16
		, void **config_data
#endif
		)
{
	(void)flags;
	(void)info;
	(void)settings;
#if SPA_VERSION_BLUEZ5_CODEC_MEDIA >= 16
	(void)config_data;
#endif
	struct a2dp_ssc conf;
	const struct ssc_profile *profile = ssc_profile_current();

	if (caps_size < sizeof(conf))
		return -EINVAL;

	memcpy(&conf, caps, sizeof(conf));
	if (conf.info.vendor_id != codec->vendor.vendor_id ||
	    conf.info.codec_id != codec->vendor.codec_id)
		return -ENOTSUP;
	if ((conf.capabilities & SSC_CAP_HIFI_24) == 0)
		return -ENOTSUP;

	conf.capabilities = profile->capabilities;

	memcpy(config, &conf, sizeof(conf));
	return sizeof(conf);
}

static int codec_validate_config(const struct media_codec *codec, uint32_t flags,
		const void *caps, size_t caps_size, struct spa_audio_info *info)
{
	(void)flags;
	const struct a2dp_ssc *conf = caps;
	const struct ssc_profile *profile = ssc_profile_current();
	int rate = 48000;
	int channels = 2;

	if (caps == NULL || caps_size < sizeof(*conf))
		return -EINVAL;
	if (conf->info.vendor_id != codec->vendor.vendor_id ||
	    conf->info.codec_id != codec->vendor.codec_id)
		return -EINVAL;
	if (!ssc_caps_valid_for_profile(conf->capabilities, profile))
		return -EINVAL;

	spa_zero(*info);
	info->media_type = SPA_MEDIA_TYPE_audio;
	info->media_subtype = SPA_MEDIA_SUBTYPE_raw;
	info->info.raw.format = SSCENC_SPA_FORMAT;
	info->info.raw.rate = (uint32_t)rate;
	info->info.raw.channels = (uint32_t)channels;
	if (channels == 1) {
		info->info.raw.position[0] = SPA_AUDIO_CHANNEL_MONO;
	} else {
		info->info.raw.position[0] = SPA_AUDIO_CHANNEL_FL;
		info->info.raw.position[1] = SPA_AUDIO_CHANNEL_FR;
	}
	return 0;
}

static int codec_enum_config(const struct media_codec *codec, uint32_t flags,
		const void *caps, size_t caps_size, uint32_t id, uint32_t idx,
		struct spa_pod_builder *b, struct spa_pod **param)
{
	struct spa_audio_info info;
	struct spa_pod_frame f[1];
	int res;

	if ((res = codec_validate_config(codec, flags, caps, caps_size, &info)) < 0)
		return res;
	if (idx > 0)
		return 0;

	spa_pod_builder_push_object(b, &f[0], SPA_TYPE_OBJECT_Format, id);
	spa_pod_builder_add(b,
			SPA_FORMAT_mediaType,      SPA_POD_Id(info.media_type),
			SPA_FORMAT_mediaSubtype,   SPA_POD_Id(info.media_subtype),
			SPA_FORMAT_AUDIO_format,   SPA_POD_Id(info.info.raw.format),
			SPA_FORMAT_AUDIO_rate,     SPA_POD_Int(info.info.raw.rate),
			SPA_FORMAT_AUDIO_channels, SPA_POD_Int(info.info.raw.channels),
			SPA_FORMAT_AUDIO_position, SPA_POD_Array(sizeof(uint32_t),
					SPA_TYPE_Id, info.info.raw.channels, info.info.raw.position),
			0);

	*param = spa_pod_builder_pop(b, &f[0]);
	return *param == NULL ? -EIO : 1;
}

static void *codec_init(const struct media_codec *codec, uint32_t flags,
		void *config, size_t config_size, const struct spa_audio_info *info,
		void *props, size_t mtu)
{
	(void)flags;
	(void)props;
	(void)mtu;
	struct impl *this;
	struct a2dp_ssc *conf = config;
	uint32_t rate, channels;
	const struct ssc_profile *profile = ssc_profile_current();

	if (config_size < sizeof(*conf) || !info ||
	    conf->info.vendor_id != codec->vendor.vendor_id ||
	    conf->info.codec_id != codec->vendor.codec_id ||
	    !ssc_caps_valid_for_profile(conf->capabilities, profile) ||
	    info->media_type != SPA_MEDIA_TYPE_audio ||
	    info->media_subtype != SPA_MEDIA_SUBTYPE_raw ||
	    info->info.raw.format != SSCENC_SPA_FORMAT)
		return NULL;

	rate = info->info.raw.rate;
	channels = info->info.raw.channels;
	if (rate != 48000 || channels == 0 || channels > 2)
		return NULL;

	this = calloc(1, sizeof(*this));
	if (this == NULL)
		return NULL;
	if (sscenc_config_basic(&this->cfg, rate, (uint8_t)channels, profile->bitrate) != SSCENC_OK)
		goto fail;
	this->enc = sscenc_create(&this->cfg);
	if (this->enc == NULL)
		goto fail;
	this->frame_samples = profile->frame_samples;
	this->block_size = this->frame_samples * (size_t)channels * SSCENC_SAMPLE_SIZE;
	return this;

fail:
	free(this);
	return NULL;
}

static void codec_deinit(void *data)
{
	struct impl *this = data;
	sscenc_destroy(this->enc);
	free(this);
}

static int codec_get_block_size(void *data)
{
	struct impl *this = data;
	return (int)this->block_size;
}

static uint64_t codec_get_interval(void *data)
{
	struct impl *this = data;
	return (uint64_t)this->frame_samples * SPA_NSEC_PER_SEC / this->cfg.sample_rate;
}

static int codec_abr_process(void *data, size_t unsent)
{
	(void)data;
	(void)unsent;
	return -ENOTSUP;
}

static int codec_start_encode(void *data, void *dst, size_t dst_size,
		uint16_t seqnum, uint32_t timestamp)
{
	struct impl *this = data;
	const size_t header_size = sizeof(struct rtp_header);

	if (dst_size < header_size)
		return -ENOSPC;

	this->header = (struct rtp_header *)dst;
	memset(this->header, 0, header_size);
	this->header->v = 2;
	this->header->pt = 96;
	this->header->sequence_number = htons(seqnum);
	this->header->timestamp = htonl(timestamp);
	this->header->ssrc = htonl(0xffu);
	return (int)header_size;
}

static int codec_encode(void *data,
		const void *src, size_t src_size,
		void *dst, size_t dst_size,
		size_t *dst_out, int *need_flush)
{
	struct impl *this = data;
	int res;

	*dst_out = 0;
	if (src == NULL)
		return -EINVAL;
	if (src_size < this->block_size)
		return 0;

#ifdef SSCENC_BLOB_HELPER
	res = sscenc_encode_s32(this->enc, src, this->frame_samples, dst, dst_size, dst_out);
#else
	res = sscenc_encode_s16(this->enc, src, this->frame_samples, dst, dst_size, dst_out);
#endif
	if (res != SSCENC_OK)
		return -EINVAL;

	*need_flush = NEED_FLUSH_ALL;
	return (int)this->block_size;
}

static void codec_get_delay(void *data, uint32_t *encoder, uint32_t *decoder)
{
	(void)data;
	if (encoder)
		*encoder = 0;
	if (decoder)
		*decoder = 0;
}

const struct media_codec a2dp_codec_ssc = {
	.id = SPA_BLUETOOTH_AUDIO_CODEC_SSC,
#if SPA_VERSION_BLUEZ5_CODEC_MEDIA >= 16
	.kind = MEDIA_CODEC_A2DP,
#endif
	.codec_id = A2DP_CODEC_VENDOR,
	.vendor = { .vendor_id = SSC_VENDOR_ID, .codec_id = SSC_CODEC_ID },
	.name = "ssc",
	.description = "Samsung SSC (experimental)",
	.fill_caps = codec_fill_caps,
	.select_config = codec_select_config,
	.enum_config = codec_enum_config,
	.validate_config = codec_validate_config,
	.init = codec_init,
	.deinit = codec_deinit,
	.get_block_size = codec_get_block_size,
	.get_interval = codec_get_interval,
	.abr_process = codec_abr_process,
	.start_encode = codec_start_encode,
	.encode = codec_encode,
	.get_delay = codec_get_delay,
};

MEDIA_CODEC_EXPORT_DEF("ssc", &a2dp_codec_ssc);
