#include <obs-module.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <opencv2/opencv.hpp>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("opencv_out", "en-US")
#define blog(level, msg, ...) blog(level, "opencv-out: " msg, ##__VA_ARGS__)

#define _MT obs_module_text

struct opencv_filter_data {
	obs_source_t *context;
	obs_data_t *settings;
	
	gs_texrender_t *texrender;
	gs_stagesurf_t *surf;
	
	uint8_t *data;
	size_t size;
	size_t linesize;
	
	int total_width;
	int total_height;

	bool read_texture;

	struct dstr path;
};

static const char *opencv_filter_get_name(void *unused){
	UNUSED_PARAMETER(unused);
	return _MT("OpencvFilter");
}

static void *opencv_filter_create(obs_data_t *settings, obs_source_t *source)
{
	struct opencv_filter_data *filter = (struct opencv_filter_data *)
			bzalloc(sizeof(struct opencv_filter_data));
	filter->context = source;
	filter->settings = settings;
	
	filter->texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	dstr_init(&filter->path);
	
	obs_source_update(source, settings);
	
	return filter;
}

static void opencv_filter_destroy(void *data)
{
	struct opencv_filter_data *filter = (struct opencv_filter_data *)data;
	obs_data_release(filter->settings);
	
	obs_enter_graphics();
	gs_texrender_destroy(filter->texrender);
	gs_stagesurface_destroy(filter->surf);
	obs_leave_graphics();
	
	dstr_free(&filter->path);
	
	bfree(filter);
}

static obs_properties_t *opencv_filter_properties(void *data)
{
	struct opencv_filter_data *filter = (struct opencv_filter_data *)data;
	obs_properties_t *props = obs_properties_create();
	
	obs_properties_add_path(props, "path", _MT("Opencv.Folder"), OBS_PATH_FILE_SAVE, NULL, NULL);
	
	return props;
}

static void opencv_filter_update(void *data, obs_data_t *settings)
{
	struct opencv_filter_data *filter = (struct opencv_filter_data *)data;
	const char* path = obs_data_get_string(settings, "path");
	dstr_free(&filter->path);
	dstr_copy(&filter->path, path);
}

static void opencv_filter_tick(void *data, float seconds)
{
	struct opencv_filter_data *filter = (struct opencv_filter_data*)data;
	obs_source_t *target = obs_filter_get_target(filter->context);
	
	int base_width = obs_source_get_base_width(target);
	int base_height = obs_source_get_base_height(target);
	
	int surf_cx = 0;
	int surf_cy = 0;
	int src_cx = obs_source_get_width(target);
	int src_cy = obs_source_get_height(target);

	if(filter->surf){
		obs_enter_graphics();
		surf_cx = gs_stagesurface_get_width(filter->surf);
		surf_cy = gs_stagesurface_get_height(filter->surf);
		if (surf_cx != src_cx || surf_cy != src_cy) {
			gs_stagesurface_destroy(filter->surf);
			filter->surf = NULL;
		}
		obs_leave_graphics();
	}
	
	size_t linesize = base_width * 4;
	size_t size = linesize * base_height;
	if(filter->data){
		if(filter->size != size)
			filter->data = (uint8_t*)brealloc(filter->data, size);
	}
	
	filter->total_width = base_width;
	filter->total_height = base_height;
	
	if (!filter->surf) {
		obs_enter_graphics();
		filter->surf = gs_stagesurface_create(base_width, base_height, GS_RGBA);
		obs_leave_graphics();
	}
	
	if(!filter->data)
		filter->data = (uint8_t*)bzalloc(size);
	filter->size = size;
	filter->linesize = linesize;
}

static gs_texture_t *render_original(void *data, gs_effect_t *effect,
		float source_cx, float source_cy)
{
	UNUSED_PARAMETER(effect);

	struct opencv_filter_data *filter = (struct opencv_filter_data *)data;
	obs_source_t *target = obs_filter_get_target(filter->context);
	obs_source_t *parent = obs_filter_get_parent(filter->context);

	if (!target || !parent)
		return NULL;

	gs_texrender_reset(filter->texrender);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	if (gs_texrender_begin(filter->texrender, source_cx, source_cy)) {
		uint32_t parent_flags = obs_source_get_output_flags(target);
		bool custom_draw = (parent_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
		bool async = (parent_flags & OBS_SOURCE_ASYNC) != 0;
		struct vec4 clear_color;

		vec4_zero(&clear_color);
		gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
		gs_ortho(0.0f, source_cx, 0.0f, source_cy,
			-100.0f, 100.0f);

		if (target == parent && !custom_draw && !async)
			obs_source_default_render(target);
		else
			obs_source_video_render(target);

		gs_texrender_end(filter->texrender);
	}

	gs_blend_state_pop();
	
	filter->read_texture = true;
	return gs_texrender_get_texture(filter->texrender);
}

static void process_surf(void *data, size_t source_cy)
{
	struct opencv_filter_data *filter = (struct opencv_filter_data *)data;
	obs_source_t *parent = obs_filter_get_parent(filter->context);
	if (!parent || !filter->context)
		return;
	/*
	const char* p = obs_source_get_name(parent);
	const char* f = obs_source_get_name(filter->context);
	*/
	uint8_t *tex_data = NULL;
	if (filter->read_texture && filter->surf) {
		filter->read_texture = false;
		if (dstr_is_empty(&filter->path))
			return;
		if (gs_stagesurface_map(filter->surf, &tex_data,
				(uint32_t*)&filter->linesize)) {
			/* write to buffer */
			/* os_unlink(path.array); */
			memcpy(filter->data, tex_data, filter->size);
			gs_stagesurface_unmap(filter->surf);
			os_quick_write_utf8_file(filter->path.array,
					(char*)filter->data, filter->size,
					false);
		}
	}
}

static void opencv_filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct opencv_filter_data *filter = (struct opencv_filter_data *)data;
	
	float src_cx = (float)obs_source_get_width(filter->context);
	float src_cy = (float)obs_source_get_height(filter->context);
	
	gs_texture_t *tex = render_original(data, NULL, src_cx, src_cy);
	if(filter->surf)
		gs_stage_texture(filter->surf, tex);
	filter->read_texture = true;
	
	obs_source_skip_video_filter(filter->context);
	
	process_surf(filter, src_cy);
}

static uint32_t opencv_filter_width(void *data)
{
	struct opencv_filter_data *filter = (struct opencv_filter_data *)data;
	return filter->total_width;
}

static uint32_t opencv_filter_height(void *data)
{
	struct opencv_filter_data *filter = (struct opencv_filter_data *)data;
	return filter->total_height;
}

static void opencv_filter_defaults(obs_data_t *settings)
{
}
		
bool obs_module_load(void)
{
	struct obs_source_info opencv_filter = { 0 };
	opencv_filter.id = "opencv_out";
	opencv_filter.type = OBS_SOURCE_TYPE_FILTER;
	opencv_filter.output_flags = OBS_SOURCE_VIDEO;
	opencv_filter.create = opencv_filter_create;
	opencv_filter.destroy = opencv_filter_destroy;
	opencv_filter.update = opencv_filter_update;
	opencv_filter.video_tick = opencv_filter_tick;
	opencv_filter.get_name = opencv_filter_get_name;
	opencv_filter.get_defaults = opencv_filter_defaults;
	opencv_filter.get_width = opencv_filter_width;
	opencv_filter.get_height = opencv_filter_height;
	opencv_filter.video_render = opencv_filter_render;
	opencv_filter.get_properties = opencv_filter_properties;

	obs_register_source(&opencv_filter);
	return true;
}

void obs_module_unload(void)
{
}