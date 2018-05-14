#pragma once

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <util/bmem.h>

#include <stdio.h>

#include <media-io/audio-math.h>
#include <util/platform.h>
#include <util/circlebuf.h>
#include <util/threading.h>

#define MT_ obs_module_text
#define TEXT_SIDECHAIN_SOURCE           MT_("Compressor.SidechainSource")
#define S_SIDECHAIN_SOURCE              "sidechain_source"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("audio-visualizer", "en-US")

struct sidechain_prop_info {
	obs_property_t *sources;
	obs_source_t *parent;
};

static bool add_sources(void *data, obs_source_t *source)
{
	struct sidechain_prop_info *info = (struct sidechain_prop_info *)data;
	uint32_t caps = obs_source_get_output_flags(source);

	if (source == info->parent)
		return true;
	if ((caps & OBS_SOURCE_AUDIO) == 0)
		return true;

	const char *name = obs_source_get_name(source);
	obs_property_list_add_string(info->sources, name, name);
	return true;
}

struct audio_visualizer_data {
	obs_source_t *source;
	float *audio_data;
	float *fft_data;
	uint8_t *fft_data_uint8;

	obs_weak_source_t *weak_sidechain;
	size_t envelope_buf_len;
	pthread_mutex_t sidechain_mutex;
	size_t max_sidechain_frames;

	size_t num_channels;

	struct circlebuf sidechain_data[MAX_AUDIO_CHANNELS];
	float *sidechain_buf[MAX_AUDIO_CHANNELS];
};

gs_texture_t *gs_create_texture_from_raw_data(uint8_t* data, uint32_t cx,
	uint32_t cy, enum gs_color_format format) {

	uint8_t *copy_data = (uint8_t *)bmemdup(data, cx * cy * 4);
	gs_texture_t *tex = NULL;

	if (copy_data) {
		tex = gs_texture_create(cx, cy, format, 1, (const uint8_t**)&data, 0);
		bfree(copy_data);
	}

	return tex;
}

float* _fft() {

	return NULL;
}

static inline obs_source_t *get_sidechain(struct audio_visualizer_data *cd)
{
	if (cd->weak_sidechain)
		return obs_weak_source_get_source(cd->weak_sidechain);
	return NULL;
}

static inline void get_sidechain_data(struct audio_visualizer_data *cd,
	const uint32_t num_samples)
{
	size_t data_size = cd->envelope_buf_len * sizeof(float);
	if (!data_size)
		return;

	pthread_mutex_lock(&cd->sidechain_mutex);
	if (cd->max_sidechain_frames < num_samples)
		cd->max_sidechain_frames = num_samples;

	if (cd->sidechain_data[0].size < data_size) {
		pthread_mutex_unlock(&cd->sidechain_mutex);
		goto clear;
	}

	for (size_t i = 0; i < cd->num_channels; i++)
		circlebuf_pop_front(&cd->sidechain_data[i],
			cd->sidechain_buf[i], data_size);

	pthread_mutex_unlock(&cd->sidechain_mutex);
	return;

clear:
	for (size_t i = 0; i < cd->num_channels; i++)
		memset(cd->sidechain_buf[i], 0, data_size);
}

static const char* audio_visualizer_name(void *unused) {
	UNUSED_PARAMETER(unused);
	return obs_module_text("Audio Visualizer");
};

static void *audio_visualizer_create(obs_data_t *settings, obs_source_t *source) {
	struct audio_visualizer_data *av = (struct audio_visualizer_data *)bmalloc(sizeof(*av));

	av->source = source;
	av->audio_data = (float *)bmalloc(sizeof(float) * 1024);
	av->fft_data = (float *)bmalloc(sizeof(float) * 1024);
	av->fft_data_uint8 = (uint8_t *)bmalloc(sizeof(uint8_t) * 1024);

	return av;
};

static void audio_visualizer_destroy(void *data) {

};

static void audio_visualizer_render(void *data, gs_effect_t *effect) {

	float *fft;
	uint8_t fft_uint8[1024];

	for (uint32_t i = 0; i < 1024; i++) {
		fft_uint8[i] = floor(fft[i] * 255 + 0.5);
	}
	gs_texture_t *tex = gs_create_texture_from_raw_data(&fft_uint8[0], 1024, 1, GS_RGBA);


};

static void audio_visualizer_filter_render(void *data, gs_effect_t *effect) {

	float *fft;
	uint8_t fft_uint8[1024];
	
	for (uint32_t i = 0; i < 1024; i++) {
		fft_uint8[i] = floor(fft[i] * 255 + 0.5);
	}
	gs_texture_t *tex = gs_create_texture_from_raw_data(&fft_uint8[0], 1024, 1, GS_RGBA);

};

static void audio_visualizer_update(void *data, obs_data_t *settings) {

};

static void audio_visualizer_tick(void *data, float seconds) {

}

static obs_properties_t *audio_visualizer_properties(void *data) {

	struct audio_visualizer_data *av = (struct audio_visualizer_data *)data;
	obs_properties_t *props = obs_properties_create();
	obs_source_t *parent = NULL;

	if (av)
		parent = obs_filter_get_parent(av->source);

	obs_property_t *sources = obs_properties_add_list(props,
		S_SIDECHAIN_SOURCE, TEXT_SIDECHAIN_SOURCE,
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(sources, obs_module_text("None"), "none");

	struct sidechain_prop_info info = { sources, parent };
	obs_enum_sources(add_sources, &info);

	UNUSED_PARAMETER(data);
	return props;
};

static void audio_visualizer_defaults(obs_data_t *s) {
	obs_data_set_default_string(s, S_SIDECHAIN_SOURCE, "none");
}

bool obs_module_load(void) {
	struct obs_source_info audio_visualizer;

	audio_visualizer.id = "audio_visualizer";
	audio_visualizer.get_name = audio_visualizer_name;
	audio_visualizer.output_flags = OBS_SOURCE_VIDEO;
	audio_visualizer.type = OBS_SOURCE_TYPE_INPUT;
	audio_visualizer.create = audio_visualizer_create;
	audio_visualizer.destroy = audio_visualizer_destroy;
	audio_visualizer.update = audio_visualizer_update;
	audio_visualizer.video_render = audio_visualizer_render;
	audio_visualizer.get_properties = audio_visualizer_properties;
	audio_visualizer.get_defaults = audio_visualizer_defaults;
	audio_visualizer.get_defaults2 = NULL;
	audio_visualizer.video_tick = audio_visualizer_tick;

	struct obs_source_info audio_visualizer_filter;

	audio_visualizer_filter.id = "audio_visualizer_filter";
	audio_visualizer_filter.get_name = audio_visualizer_name;
	audio_visualizer_filter.output_flags = OBS_SOURCE_VIDEO;
	audio_visualizer_filter.type = OBS_SOURCE_TYPE_FILTER;
	audio_visualizer_filter.create = audio_visualizer_create;
	audio_visualizer_filter.destroy = audio_visualizer_destroy;
	audio_visualizer_filter.update = audio_visualizer_update;
	audio_visualizer_filter.video_render = audio_visualizer_filter_render;
	audio_visualizer_filter.get_properties = audio_visualizer_properties;
	audio_visualizer_filter.get_defaults = audio_visualizer_defaults;
	audio_visualizer_filter.get_defaults2 = NULL;
	audio_visualizer_filter.video_tick = audio_visualizer_tick;

	obs_register_source(&audio_visualizer);
	obs_register_source(&audio_visualizer_filter);

	return true;
}
