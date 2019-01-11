#include <obs-module.h>
#include "obs-filters-config.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-filters", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return "OBS core filters";
}

extern struct obs_source_info mask_filter;
extern struct obs_source_info crop_filter;
//extern struct obs_source_info gain_filter;
extern struct obs_source_info color_filter;
extern struct obs_source_info scale_filter;
extern struct obs_source_info scroll_filter;
extern struct obs_source_info gpu_delay_filter;
extern struct obs_source_info color_key_filter;
extern struct obs_source_info color_grade_filter;
extern struct obs_source_info sharpness_filter;
extern struct obs_source_info chroma_key_filter;
extern struct obs_source_info async_delay_filter;
#if SPEEXDSP_ENABLED
extern struct obs_source_info noise_suppress_filter;
#endif
extern struct obs_source_info invert_polarity_filter;
extern struct obs_source_info noise_gate_filter;
extern struct obs_source_info compressor_filter;

//extern struct obs_modeless_ui custom_gain_ui;

extern const char *gain_name(void *unused);
extern void gain_destroy(void *data);
extern void gain_update(void *data, obs_data_t *s);
extern void *gain_create(obs_data_t *settings, obs_source_t *filter);
extern struct obs_audio_data *gain_filter_audio(void *data,
	struct obs_audio_data *audio);
extern void gain_defaults(obs_data_t *s);
extern obs_properties_t *gain_properties(void *data);
extern void *gain_ui(void *source, void *parent);

bool obs_module_load(void)
{
	struct obs_source_info gain_filter = {
		.id = "gain_filter",
		.type = OBS_SOURCE_TYPE_FILTER,
		.output_flags = OBS_SOURCE_AUDIO,
		.get_name = gain_name,
		.create = gain_create,
		.destroy = gain_destroy,
		.update = gain_update,
		.filter_audio = gain_filter_audio,
		.get_defaults = gain_defaults,
		.get_properties = gain_properties,
	};

	struct obs_modeless_ui custom_gain_ui = {
		.id = "gain_filter",
		.task = "properties",
		.target = "qt",
		.create = gain_ui,
	};

	obs_register_source(&mask_filter);
	obs_register_source(&crop_filter);
	obs_register_source(&gain_filter);
	obs_register_source(&color_filter);
	obs_register_source(&scale_filter);
	obs_register_source(&scroll_filter);
	obs_register_source(&gpu_delay_filter);
	obs_register_source(&color_key_filter);
	obs_register_source(&color_grade_filter);
	obs_register_source(&sharpness_filter);
	obs_register_source(&chroma_key_filter);
	obs_register_source(&async_delay_filter);
#if SPEEXDSP_ENABLED
	obs_register_source(&noise_suppress_filter);
#endif
	obs_register_source(&invert_polarity_filter);
	obs_register_source(&noise_gate_filter);
	obs_register_source(&compressor_filter);

	obs_register_modeless_ui(&custom_gain_ui);
	return true;
}
