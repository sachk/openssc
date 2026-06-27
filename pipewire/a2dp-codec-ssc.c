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
#define SSC_CAPABILITIES 0x3eu

#define SSC_DEFAULT_BITRATE 192000u
#ifdef SSCENC_BLOB_HELPER
#define SSCENC_SPA_FORMAT SPA_AUDIO_FORMAT_S32
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
	size_t block_size;
};


static int codec_fill_caps(const struct media_codec *codec, uint32_t flags,
		const struct spa_dict *settings, uint8_t caps[A2DP_MAX_CAPS_SIZE])
{
	(void)codec;
	(void)flags;
	(void)settings;
	static const struct a2dp_ssc a2dp_ssc = {
		.info.vendor_id = SSC_VENDOR_ID,
		.info.codec_id = SSC_CODEC_ID,
		.capabilities = SSC_CAPABILITIES,
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

	if (caps_size < sizeof(conf))
		return -EINVAL;

	memcpy(&conf, caps, sizeof(conf));
	if (conf.info.vendor_id != codec->vendor.vendor_id ||
	    conf.info.codec_id != codec->vendor.codec_id)
		return -ENOTSUP;
	if ((conf.capabilities & SSC_CAPABILITIES) == 0)
		return -ENOTSUP;

	conf.capabilities &= SSC_CAPABILITIES;

	memcpy(config, &conf, sizeof(conf));
	return sizeof(conf);
}

static int codec_validate_config(const struct media_codec *codec, uint32_t flags,
		const void *caps, size_t caps_size, struct spa_audio_info *info)
{
	(void)flags;
	const struct a2dp_ssc *conf = caps;
	int rate = 48000;
	int channels = 2;

	if (caps == NULL || caps_size < sizeof(*conf))
		return -EINVAL;
	if (conf->info.vendor_id != codec->vendor.vendor_id ||
	    conf->info.codec_id != codec->vendor.codec_id)
		return -EINVAL;
	if ((conf->capabilities & SSC_CAPABILITIES) == 0)
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

	if (config_size < sizeof(*conf) || !info ||
	    conf->info.vendor_id != codec->vendor.vendor_id ||
	    conf->info.codec_id != codec->vendor.codec_id ||
	    (conf->capabilities & SSC_CAPABILITIES) == 0 ||
	    info->media_type != SPA_MEDIA_TYPE_audio ||
	    info->media_subtype != SPA_MEDIA_SUBTYPE_raw ||
	    info->info.raw.format != SSCENC_SPA_FORMAT)
		return NULL;

	rate = info->info.raw.rate;
	channels = info->info.raw.channels;
	if ((rate != 44100 && rate != 48000) || channels == 0 || channels > 2)
		return NULL;

	this = calloc(1, sizeof(*this));
	if (this == NULL)
		return NULL;
	if (sscenc_config_basic(&this->cfg, rate, (uint8_t)channels, SSC_DEFAULT_BITRATE) != SSCENC_OK)
		goto fail;
	this->enc = sscenc_create(&this->cfg);
	if (this->enc == NULL)
		goto fail;
	this->block_size = SSCENC_FRAME_SAMPLES * (size_t)channels * SSCENC_SAMPLE_SIZE;
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
	return (uint64_t)SSCENC_FRAME_SAMPLES * SPA_NSEC_PER_SEC / this->cfg.sample_rate;
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
	this->header->ssrc = htonl(1);
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
	res = sscenc_encode_s32(this->enc, src, SSCENC_FRAME_SAMPLES, dst, dst_size, dst_out);
#else
	res = sscenc_encode_s16(this->enc, src, SSCENC_FRAME_SAMPLES, dst, dst_size, dst_out);
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
