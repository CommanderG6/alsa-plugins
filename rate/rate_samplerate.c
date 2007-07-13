/*
 * Rate converter plugin using libsamplerate
 *
 * Copyright (c) 2006 by Takashi Iwai <tiwai@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <samplerate.h>
#include <alsa/asoundlib.h>
#include <alsa/pcm_rate.h>

struct rate_src {
	double ratio;
	int converter;
	unsigned int channels;
	float *src_buf;
	float *dst_buf;
	SRC_STATE *state;
	SRC_DATA data;
};

static snd_pcm_uframes_t input_frames(void *obj, snd_pcm_uframes_t frames)
{
	struct rate_src *rate = obj;
	if (frames == 0)
		return 0;
	return (snd_pcm_uframes_t)(frames / rate->ratio);
}

static snd_pcm_uframes_t output_frames(void *obj, snd_pcm_uframes_t frames)
{
	struct rate_src *rate = obj;
	if (frames == 0)
		return 0;
	return (snd_pcm_uframes_t)(frames * rate->ratio);
}

static void pcm_src_free(void *obj)
{
	struct rate_src *rate = obj;

	free(rate->src_buf);
	free(rate->dst_buf);
	rate->src_buf = rate->dst_buf = NULL;

	if (rate->state) {
		src_delete(rate->state);
		rate->state = NULL;
	}
}

static int pcm_src_init(void *obj, snd_pcm_rate_info_t *info)
{
	struct rate_src *rate = obj;
	int err;

	if (! rate->state || rate->channels != info->channels) {
		if (rate->state)
			src_delete(rate->state);
		rate->channels = info->channels;
		rate->state = src_new(rate->converter, rate->channels, &err);
		if (! rate->state)
			return -EINVAL;
	}

	rate->ratio = (double)info->out.rate / (double)info->in.rate;

	free(rate->src_buf);
	rate->src_buf = malloc(sizeof(float) * rate->channels * info->in.period_size);
	free(rate->dst_buf);
	rate->dst_buf = malloc(sizeof(float) * rate->channels * info->out.period_size);
	if (! rate->src_buf || ! rate->dst_buf) {
		pcm_src_free(rate);
		return -ENOMEM;
	}

	rate->data.data_in = rate->src_buf;
	rate->data.data_out = rate->dst_buf;
	rate->data.src_ratio = rate->ratio;
	rate->data.end_of_input = 0;

	return 0;
}

static int pcm_src_adjust_pitch(void *obj, snd_pcm_rate_info_t *info)
{
	struct rate_src *rate = obj;

	rate->ratio = ((double)info->out.period_size / (double)info->in.period_size);
	rate->data.src_ratio = rate->ratio;
	return 0;
}

static void pcm_src_reset(void *obj)
{
	struct rate_src *rate = obj;

	src_reset(rate->state);
}

static void pcm_src_convert_s16(void *obj, int16_t *dst, unsigned int dst_frames,
				const int16_t *src, unsigned int src_frames)
{
	struct rate_src *rate = obj;
	unsigned int ofs;

	rate->data.input_frames = src_frames;
	rate->data.output_frames = dst_frames;
	rate->data.end_of_input = 0;
	
	src_short_to_float_array(src, rate->src_buf, src_frames * rate->channels);
	src_process(rate->state, &rate->data);
	if (rate->data.output_frames_gen < dst_frames)
		ofs = dst_frames - rate->data.output_frames_gen;
	else
		ofs = 0;
	src_float_to_short_array(rate->dst_buf, dst + ofs * rate->channels,
				 rate->data.output_frames_gen * rate->channels);
}

static void pcm_src_close(void *obj)
{
	free(obj);
}

static snd_pcm_rate_ops_t pcm_src_ops = {
	.close = pcm_src_close,
	.init = pcm_src_init,
	.free = pcm_src_free,
	.reset = pcm_src_reset,
	.adjust_pitch = pcm_src_adjust_pitch,
	.convert_s16 = pcm_src_convert_s16,
	.input_frames = input_frames,
	.output_frames = output_frames,
};

static int pcm_src_open(unsigned int version, void **objp,
			snd_pcm_rate_ops_t *ops, int type)
{
	struct rate_src *rate;

	if (version != SND_PCM_RATE_PLUGIN_VERSION) {
		fprintf(stderr, "Invalid rate plugin version %x\n", version);
		return -EINVAL;
	}

	rate = calloc(1, sizeof(*rate));
	if (! rate)
		return -ENOMEM;
	rate->converter = type;

	*objp = rate;
	*ops = pcm_src_ops;
	return 0;
}

int SND_PCM_RATE_PLUGIN_ENTRY(samplerate) (unsigned int version, void **objp,
					   snd_pcm_rate_ops_t *ops)
{
	return pcm_src_open(version, objp, ops, SRC_SINC_FASTEST);
}

int SND_PCM_RATE_PLUGIN_ENTRY(samplerate_best) (unsigned int version, void **objp,
						snd_pcm_rate_ops_t *ops)
{
	return pcm_src_open(version, objp, ops, SRC_SINC_BEST_QUALITY);
}

int SND_PCM_RATE_PLUGIN_ENTRY(samplerate_medium) (unsigned int version, void **objp,
						  snd_pcm_rate_ops_t *ops)
{
	return pcm_src_open(version, objp, ops, SRC_SINC_MEDIUM_QUALITY);
}

int SND_PCM_RATE_PLUGIN_ENTRY(samplerate_order) (unsigned int version, void **objp,
						 snd_pcm_rate_ops_t *ops)
{
	return pcm_src_open(version, objp, ops, SRC_ZERO_ORDER_HOLD);
}

int SND_PCM_RATE_PLUGIN_ENTRY(samplerate_linear) (unsigned int version, void **objp,
						  snd_pcm_rate_ops_t *ops)
{
	return pcm_src_open(version, objp, ops, SRC_LINEAR);
}
