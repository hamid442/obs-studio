#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <util/base.h>
#include <util/dstr.h>
#include <util/platform.h>

#include <float.h>
#include <limits.h>
#include <stdio.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs_shader_filter", "en-US")

#define _MT obs_module_text

static const char *effect_template_default_image_shader =
"\
uniform float4x4 ViewProj;\
uniform texture2d image;\
\
uniform float elapsed_time;\
uniform float2 uv_offset;\
uniform float2 uv_scale;\
uniform float2 uv_pixel_interval;\
\
sampler_state textureSampler{\
	Filter = Linear;\
	AddressU = Border;\
	AddressV = Border;\
	BorderColor = 00000000;\
};\
\
struct VertData {\
	float4 pos : POSITION;\
	float2 uv : TEXCOORD0;\
};\
\
VertData mainTransform(VertData v_in)\
{\
	VertData vert_out;\
	vert_out.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);\
	vert_out.uv = v_in.uv * uv_scale + uv_offset;\
	return vert_out;\
}\
\
\r\
float4 mainImage(VertData v_in) : TARGET\r\
{\r\
	return image.Sample(textureSampler, v_in.uv);\r\
}\r\
\
technique Draw\
{\
	pass\
	{\
		vertex_shader = mainTransform(v_in);\
		pixel_shader = mainImage(v_in);\
	}\
}";

struct effect_param_data
{
	struct dstr name;
	struct dstr desc;
	enum gs_shader_param_type type;
	gs_eparam_t *param;

	gs_image_file_t *image;

	bool is_vec4;

	union
	{
		long long i;
		double f;
	} value;
};

struct shader_filter_data
{
	obs_source_t *context;
	gs_effect_t *effect;

	bool reload_effect;
	struct dstr last_path;
	bool last_from_file;

	gs_eparam_t *param_uv_offset;
	gs_eparam_t *param_uv_scale;
	gs_eparam_t *param_uv_pixel_interval;
	gs_eparam_t *param_elapsed_time;

	int expand_left;
	int expand_right;
	int expand_top;
	int expand_bottom;

	int total_width;
	int total_height;

	struct vec2 uv_offset;
	struct vec2 uv_scale;
	struct vec2 uv_pixel_interval;
	float elapsed_time;

	DARRAY(struct effect_param_data) stored_param_list;
};



static void shader_filter_reload_effect(struct shader_filter_data *filter)
{
	obs_data_t *settings = obs_source_get_settings(filter->context);

	// First, clean up the old effect and all references to it. 
	size_t param_count = filter->stored_param_list.num;
	for (size_t param_index = 0; param_index < param_count; param_index++)
	{
		struct effect_param_data *param =
			(filter->stored_param_list.array + param_index);
		if (param->image != NULL)
		{
			obs_enter_graphics();
			gs_image_file_free(param->image);
			obs_leave_graphics();

			bfree(param->image);
			param->image = NULL;
		}
	}

	da_free(filter->stored_param_list);

	filter->param_elapsed_time = NULL;
	filter->param_uv_offset = NULL;
	filter->param_uv_pixel_interval = NULL;
	filter->param_uv_scale = NULL;

	if (filter->effect != NULL)
	{
		obs_enter_graphics();
		gs_effect_destroy(filter->effect);
		filter->effect = NULL;
		obs_leave_graphics();
	}

	//load text
	const char *shader_text = NULL;

	const char *file_name = obs_data_get_string(settings,
		"shader_file_name");


	if (file_name)
		shader_text = os_quick_read_utf8_file(file_name);
	else
		shader_text = effect_template_default_image_shader;


	if (shader_text == NULL)
		shader_text = "";

	struct dstr effect_text = { 0 };

	dstr_cat(&effect_text, shader_text);

	// Create the effect. 
	char *errors = NULL;

	obs_enter_graphics();
	filter->effect = gs_effect_create(effect_text.array, NULL, &errors);
	obs_leave_graphics();

	dstr_free(&effect_text);

	if (filter->effect == NULL)
	{
		blog(LOG_WARNING,
			"[obs-shader-filter] Unable to create effect."
			"Errors returned from parser:\n%s",
			(errors == NULL || strlen(errors) == 0 ?
				"(None)" : errors));
	}

	// Store references to the new effect's parameters. 
	da_init(filter->stored_param_list);
	size_t effect_count = gs_effect_get_num_params(filter->effect);
	for (size_t effect_index = 0; effect_index < effect_count;
		effect_index++)
	{
		gs_eparam_t *param = gs_effect_get_param_by_idx(filter->effect,
			effect_index);

		struct gs_effect_param_info info;
		gs_effect_get_param_info(param, &info);

		if (strcmp(info.name, "uv_offset") == 0)
			filter->param_uv_offset = param;

		else if (strcmp(info.name, "uv_scale") == 0)
			filter->param_uv_scale = param;

		else if (strcmp(info.name, "uv_pixel_interval") == 0)
			filter->param_uv_pixel_interval = param;

		else if (strcmp(info.name, "elapsed_time") == 0)
			filter->param_elapsed_time = param;

		else if (strcmp(info.name, "ViewProj") == 0 ||
			strcmp(info.name, "image") == 0)
		{
			// Nothing.
		} else
		{
			struct effect_param_data *cached_data =
				da_push_back_new(filter->stored_param_list);
			dstr_copy(&cached_data->name, info.name);
			cached_data->type = info.type;
			cached_data->param = param;
		}
	}
}

static const char *shader_filter_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("ShaderFilter");
}

static void *shader_filter_create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(source);

	struct shader_filter_data *filter =
		bzalloc(sizeof(struct shader_filter_data));

	filter->context = source;
	filter->reload_effect = true;

	dstr_init(&filter->last_path);

	dstr_copy(&filter->last_path, obs_data_get_string(settings,
		"shader_file_name"));

	filter->last_from_file = obs_data_get_bool(settings,
		"from_file");

	da_init(filter->stored_param_list);

	obs_source_update(source, settings);

	return filter;
}

static void shader_filter_destroy(void *data)
{
	struct shader_filter_data *filter = data;

	dstr_free(&filter->last_path);
	da_free(filter->stored_param_list);

	bfree(filter);
}

static bool shader_filter_file_name_changed(obs_properties_t *props,
	obs_property_t *p, obs_data_t *settings)
{
	struct shader_filter_data *filter = obs_properties_get_param(props);
	const char *new_file_name = obs_data_get_string(settings,
		obs_property_name(p));

	if (dstr_is_empty(&filter->last_path) || dstr_cmp(&filter->last_path,
		new_file_name) != 0)
	{
		filter->reload_effect = true;
		dstr_copy(&filter->last_path, new_file_name);
	}

	return true;
}

static bool shader_filter_reload_effect_clicked(obs_properties_t *props,
	obs_property_t *property, void *data)
{
	struct shader_filter_data *filter = data;

	filter->reload_effect = true;

	obs_source_update(filter->context, NULL);

	return true;
}

static const char *shader_filter_texture_file_filter =
"Textures (*.bmp *.tga *.png *.jpeg *.jpg *.gif);;";

bool get_eparam_bool(gs_eparam_t *param, const char* name, bool default_value)
{
	gs_eparam_t *note = gs_param_get_annotation_by_name(param, name);
	struct gs_effect_param_info note_info;

	void* val = NULL;

	gs_effect_get_param_info(note, &note_info);

	bool is_true = default_value;

	if (note) {
		if (note_info.type == GS_SHADER_PARAM_FLOAT) {
			val = (void *)gs_effect_get_default_val(note);

			if (val) {
				is_true = *((float*)val) != 0.0;
				bfree(val);
				val = NULL;
			}
		} else if (note_info.type == GS_SHADER_PARAM_INT ||
			note_info.type == GS_SHADER_PARAM_BOOL) {
			val = (void *)gs_effect_get_default_val(note);

			if (val) {
				is_true = *((int*)val) != 0;
				bfree(val);
				val = NULL;
			}
		}
	}

	return is_true;
}

int get_eparam_int(gs_eparam_t *param, const char* name, int default_value)
{
	gs_eparam_t *note = gs_param_get_annotation_by_name(param, name);
	struct gs_effect_param_info note_info;

	void* val = NULL;
	int ret = default_value;

	gs_effect_get_param_info(note, &note_info);

	if (note) {
		if (note_info.type == GS_SHADER_PARAM_FLOAT) {
			val = (void*)gs_effect_get_default_val(note);
			if (val) {
				ret = (int)*((float*)val);
				bfree(val);
				val = NULL;
			}
		} else if (note_info.type == GS_SHADER_PARAM_INT ||
			note_info.type == GS_SHADER_PARAM_BOOL) {
			val = (void*)gs_effect_get_default_val(note);
			if (val) {
				ret = *((int*)val);
				bfree(val);
				val = NULL;
			}
		}
	}

	return ret;
}

float get_eparam_float(gs_eparam_t *param, const char* name, float default_value)
{
	gs_eparam_t *note = gs_param_get_annotation_by_name(param, name);
	struct gs_effect_param_info note_info;

	void* val = NULL;
	float ret = default_value;

	gs_effect_get_param_info(note, &note_info);

	if (note) {
		if (note_info.type == GS_SHADER_PARAM_FLOAT) {
			val = (void*)gs_effect_get_default_val(note);
			if (val) {
				ret = *((float*)val);
				bfree(val);
				val = NULL;
			}
		} else if (note_info.type == GS_SHADER_PARAM_INT ||
			note_info.type == GS_SHADER_PARAM_BOOL) {
			val = (void*)gs_effect_get_default_val(note);
			if (val) {
				ret = (float)*((int*)val);
				bfree(val);
				val = NULL;
			}
		}
	}

	return ret;
}

/* free w/ bfree */
char *get_eparam_string(gs_eparam_t *param, const char* name,
	const char* default_value)
{
	gs_eparam_t *note = gs_param_get_annotation_by_name(param, name);
	struct gs_effect_param_info note_info;

	char* val = NULL;
	struct dstr ret;
	dstr_init_copy(&ret, default_value);

	gs_effect_get_param_info(note, &note_info);

	if (note && note_info.type == GS_SHADER_PARAM_STRING) {
		val = (char*)gs_effect_get_default_val(note);
		if (val) {
			dstr_copy(&ret, val);
			bfree(val);
			val = NULL;
		}
	}

	return ret.array;
}

static obs_properties_t *shader_filter_properties(void *data)
{
	struct shader_filter_data *filter = data;

	struct dstr shaders_path = { 0 };
	dstr_init(&shaders_path);
	dstr_cat(&shaders_path, obs_get_module_data_path(obs_current_module()));
	dstr_cat(&shaders_path, "/shaders");

	obs_properties_t *props = obs_properties_create();

	obs_properties_set_param(props, filter, NULL);

	obs_properties_add_button(props, "reload_effect",
		obs_module_text("ShaderFilter.ReloadEffect"),
		shader_filter_reload_effect_clicked);

	//todo: use annotations to make these settings visible or not
	obs_properties_add_int_slider(props, "expand_left",
		obs_module_text("ShaderFilter.ExpandLeft"), 0, 9999, 1);
	obs_properties_add_int_slider(props, "expand_right",
		obs_module_text("ShaderFilter.ExpandRight"), 0, 9999, 1);
	obs_properties_add_int_slider(props, "expand_top",
		obs_module_text("ShaderFilter.ExpandTop"), 0, 9999, 1);
	obs_properties_add_int_slider(props, "expand_bottom",
		obs_module_text("ShaderFilter.ExpandBottom"), 0, 9999, 1);

	obs_property_t *file_name = obs_properties_add_path(props,
		"shader_file_name",
		obs_module_text("ShaderFilter.ShaderFileName"), OBS_PATH_FILE,
		NULL, shaders_path.array);

	obs_property_set_modified_callback(file_name,
		shader_filter_file_name_changed);


	size_t param_count = filter->stored_param_list.num;
	for (size_t param_index = 0; param_index < param_count; param_index++)
	{
		struct effect_param_data *param =
			(filter->stored_param_list.array + param_index);

		const char *param_name = param->name.array;
		gs_eparam_t *note;
		struct gs_effect_param_info note_info;
		float f_min = -FLT_MAX;
		float f_max = FLT_MAX;
		float f_step = 1.0;
		float *f_tmp = NULL;
		int i_min = INT_MIN;
		int i_max = INT_MAX;
		int i_step = 1;
		int *i_tmp = NULL;
		char *c_tmp = NULL;
		bool uses_module_text = false;
		bool is_vec4 = false;
		struct dstr n_param_name;
		struct dstr n_param_desc;
		bool is_slider = false;

		/* extract params that we expect */

		/* handles <...[int|float] min [=]...>*/
		f_min = get_eparam_float(param->param, "min", f_min);
		i_min = get_eparam_int(param->param, "min", i_min);
		/*
		note = gs_param_get_annotation_by_name(param->param, "min");
		gs_effect_get_param_info(note, &note_info);

		if (note) {
			if (note_info.type == GS_SHADER_PARAM_FLOAT) {
				f_tmp = (double*)gs_effect_get_default_val(note);
				if (f_tmp) {
					f_min = *f_tmp;
					i_min = (int)f_min;
					bfree(f_tmp);
					f_tmp = NULL;
				}
			} else if (note_info.type == GS_SHADER_PARAM_INT) {
				i_tmp = (int*)gs_effect_get_default_val(note);
				if (i_tmp) {
					i_min = *i_tmp;
					f_min = (float)i_min;
					bfree(i_tmp);
					i_tmp = NULL;
				}
			}
		}
		*/
		/* handles <...[int|float] max [=]...>*/
		f_max = get_eparam_float(param->param, "max", f_max);
		i_max = get_eparam_int(param->param, "max", i_max);
		/*
		note = gs_param_get_annotation_by_name(param->param, "max");
		gs_effect_get_param_info(note, &note_info);

		if (note) {
			if (note_info.type == GS_SHADER_PARAM_FLOAT) {
				f_tmp = (double*)gs_effect_get_default_val(note);
				if (f_tmp) {
					f_max = *f_tmp;
					i_max = (int)f_max;
					bfree(f_tmp);
					f_tmp = NULL;
				}
			} else if (note_info.type == GS_SHADER_PARAM_INT) {
				i_tmp = (int*)gs_effect_get_default_val(note);
				if (i_tmp) {
					i_max = *i_tmp;
					f_max = (float)i_max;
					bfree(i_tmp);
					i_tmp = NULL;
				}
			}
		}
		*/
		/* handles <...[int|float] step [=];...>*/
		f_step = get_eparam_float(param->param, "step", f_step);
		i_step = get_eparam_int(param->param, "step", i_step);
		/*
		note = gs_param_get_annotation_by_name(param->param, "step");
		gs_effect_get_param_info(note, &note_info);

		if (note) {
			if (note_info.type == GS_SHADER_PARAM_FLOAT) {
				f_tmp = (double*)gs_effect_get_default_val(note);
				if (f_tmp) {
					f_step = *f_tmp;
					i_step = (int)f_step;
					bfree(f_tmp);
					f_tmp = NULL;
				}
			} else if (note_info.type == GS_SHADER_PARAM_INT) {
				i_tmp = (int*)gs_effect_get_default_val(note);
				if (i_tmp) {
					i_step = *i_tmp;
					f_step = (float)i_step;
					bfree(i_tmp);
					i_tmp = NULL;
				}
			}
		}
		*/

		/* handles <...bool module_text [= true|false];...>*/
		uses_module_text = get_eparam_bool(param->param, "module_text", false);

		/*
		note = gs_param_get_annotation_by_name(param->param, "module_text");
		gs_effect_get_param_info(note, &note_info);

		if (note) {
			uses_module_text = true;
			if (note_info.type == GS_SHADER_PARAM_FLOAT) {
				f_tmp = (double*)gs_effect_get_default_val(note);

				if (f_tmp) {
					uses_module_text = *f_tmp != 0.0;
					bfree(f_tmp);
					f_tmp = NULL;
				}
			} else if (note_info.type == GS_SHADER_PARAM_INT ||
				note_info.type == GS_SHADER_PARAM_BOOL) {
				i_tmp = (int*)gs_effect_get_default_val(note);

				if (i_tmp) {
					uses_module_text = *i_tmp != 0;
					bfree(i_tmp);
					i_tmp = NULL;
				}
			}
		}
		*/

		/* handles <...string name [= '...'|= "..."];...>*/
		char* desc = get_eparam_string(param->param, "name", param_name);
		if (desc) {
			if (uses_module_text) {
				dstr_copy(&param->desc, desc);
			} else {
				dstr_copy(&param->desc, _MT(desc));
			}
			bfree(desc);
		} else {
			if (uses_module_text)
				dstr_copy_dstr(&param->desc, &param->name);
			else
				dstr_copy_dstr(&param->desc, _MT(&param->name));
		}

		/*
		note = gs_param_get_annotation_by_name(param->param, "name");
		gs_effect_get_param_info(note, &note_info);

		if (note && note_info.type == GS_SHADER_PARAM_STRING) {
			c_tmp = (char*)gs_effect_get_default_val(note);
			if (c_tmp) {
				if (uses_module_text)
					dstr_copy(&param->desc, c_tmp);
				else
					dstr_copy(&param->desc, _MT(c_tmp));

				bfree(c_tmp);
				c_tmp = NULL;
			} else {
				if (uses_module_text)
					dstr_copy_dstr(&param->desc, &param->name);
				else
					dstr_copy_dstr(&param->desc, _MT(&param->name));
			}
		} else {
			if (uses_module_text)
				dstr_copy_dstr(&param->desc, &param->name);
			else
				dstr_copy_dstr(&param->desc, _MT(&param->name));
		}
		*/
		const char *param_desc = param->desc.array;

		/*todo: control gui elements added via annotations*/
		switch (param->type)
		{
		case GS_SHADER_PARAM_BOOL:
			obs_properties_add_bool(props, param_name, param_desc);

			//obs_properties_add_list(props, param_name, param_name, type, format);
			break;
		case GS_SHADER_PARAM_FLOAT:
		case GS_SHADER_PARAM_INT:
			note = gs_param_get_annotation_by_name(param->param, "is_slider");
			gs_effect_get_param_info(note, &note_info);

			bool is_float = param->type == GS_SHADER_PARAM_FLOAT;

			if (note) {
				
				is_slider = true;
				if (note_info.type == GS_SHADER_PARAM_FLOAT) {
					f_tmp = (double*)gs_effect_get_default_val(note);

					if (f_tmp) {
						is_slider = *f_tmp != 0.0;
						bfree(f_tmp);
						f_tmp = NULL;
					}
				} else if (note_info.type == GS_SHADER_PARAM_INT ||
							note_info.type == GS_SHADER_PARAM_BOOL) {
					i_tmp = (int*)gs_effect_get_default_val(note);
					
					if (i_tmp) {
						is_slider = *i_tmp != 0;
						bfree(i_tmp);
						i_tmp = NULL;
					}
				}

				if (is_float) {
					if (is_slider)
						obs_properties_add_float_slider(props, param_name,
							param_desc, f_min, f_max, f_step);
					else
						obs_properties_add_float(props, param_name,
							param_desc, f_min, f_max, f_step);
				} else {
					if (is_slider)
						obs_properties_add_int_slider(props, param_name,
							param_desc, i_min, i_max, i_step);
					else
						obs_properties_add_int(props, param_name,
							param_desc, i_min, i_max, i_step);
				}

			} else {
				if(is_float)
					obs_properties_add_float(props, param_name,
						param_desc, f_min, f_max, f_step);
				else
					obs_properties_add_int(props, param_name,
						param_desc, i_min, i_max, i_step);
			}

			break;
		case GS_SHADER_PARAM_INT2:
		case GS_SHADER_PARAM_INT3:
		case GS_SHADER_PARAM_INT4:
			is_slider = get_eparam_bool(param->param, "is_slider", false);

			dstr_copy(&n_param_name, param_name);
			dstr_cat(&n_param_name, ".x");
			dstr_copy(&n_param_desc, param_desc);
			dstr_cat(&n_param_desc, ".x");
			if(is_slider)
				obs_properties_add_int_slider(props, n_param_name.array,
					n_param_desc.array, i_min, i_max, i_step);
			else
				obs_properties_add_int(props, n_param_name.array,
					n_param_desc.array, i_min, i_max, i_step);

			dstr_copy(&n_param_name, param_name);
			dstr_cat(&n_param_name, ".y");
			dstr_copy(&n_param_desc, param_desc);
			dstr_cat(&n_param_desc, ".y");
			if (is_slider)
				obs_properties_add_int_slider(props, n_param_name.array,
					n_param_desc.array, i_min, i_max, i_step);
			else
				obs_properties_add_int(props, n_param_name.array,
					n_param_desc.array, i_min, i_max, i_step);

			if (param->type == GS_SHADER_PARAM_INT2)
				break;

			dstr_copy(&n_param_name, param_name);
			dstr_cat(&n_param_name, ".z");
			dstr_copy(&n_param_desc, param_desc);
			dstr_cat(&n_param_desc, ".z");
			if (is_slider)
				obs_properties_add_int_slider(props, n_param_name.array,
					n_param_desc.array, i_min, i_max, i_step);
			else
				obs_properties_add_int(props, n_param_name.array,
					n_param_desc.array, i_min, i_max, i_step);

			if (param->type == GS_SHADER_PARAM_INT3)
				break;

			dstr_copy(&n_param_name, param_name);
			dstr_cat(&n_param_name, ".w");
			dstr_copy(&n_param_desc, param_desc);
			dstr_cat(&n_param_desc, ".w");
			if (is_slider)
				obs_properties_add_int_slider(props, n_param_name.array,
					n_param_desc.array, i_min, i_max, i_step);
			else
				obs_properties_add_int(props, n_param_name.array,
					n_param_desc.array, i_min, i_max, i_step);

			break;
		case GS_SHADER_PARAM_VEC2:
		case GS_SHADER_PARAM_VEC3:
		case GS_SHADER_PARAM_VEC4:
			is_vec4 = param->type == GS_SHADER_PARAM_VEC4 &&
				get_eparam_bool(param->param, "is_vec4", false);

			/*
			note = gs_param_get_annotation_by_name(param->param, "is_vec4");
			gs_effect_get_param_info(note, &note_info);

			bool is_vec4 = false;

			if (note) {
				if (note_info.type == GS_SHADER_PARAM_FLOAT) {
					f_tmp = (double*)gs_effect_get_default_val(note);

					if (f_tmp) {
						is_vec4 = *f_tmp != 0.0;
						bfree(f_tmp);
						f_tmp = NULL;
					}
				} else if (note_info.type == GS_SHADER_PARAM_INT ||
					note_info.type == GS_SHADER_PARAM_BOOL) {
					i_tmp = (int*)gs_effect_get_default_val(note);

					if (i_tmp) {
						is_vec4 = *i_tmp != 0;
						bfree(i_tmp);
						i_tmp = NULL;
					}
				}
			}
			*/
			if (!is_vec4 && param->type == GS_SHADER_PARAM_VEC4) {
				obs_properties_add_color(props, param_name, param_desc);
				break;
			}
			is_slider = get_eparam_bool(param->param, "is_slider", false);

			dstr_copy(&n_param_name, param_name);
			dstr_cat(&n_param_name, ".x");
			dstr_copy(&n_param_desc, param_desc);
			dstr_cat(&n_param_desc, ".x");
			if(is_slider)
				obs_properties_add_float_slider(props, n_param_name.array,
					n_param_desc.array, f_min, f_max, f_step);
			else
				obs_properties_add_float(props, n_param_name.array,
					n_param_desc.array, f_min, f_max, f_step);

			dstr_copy(&n_param_name, param_name);
			dstr_cat(&n_param_name, ".y");
			dstr_copy(&n_param_desc, param_desc);
			dstr_cat(&n_param_desc, ".y");
			if (is_slider)
				obs_properties_add_float_slider(props, n_param_name.array,
					n_param_desc.array, f_min, f_max, f_step);
			else
				obs_properties_add_float(props, n_param_name.array,
					n_param_desc.array, f_min, f_max, f_step);

			if (param->type == GS_SHADER_PARAM_VEC2)
				break;

			dstr_copy(&n_param_name, param_name);
			dstr_cat(&n_param_name, ".z");
			dstr_copy(&n_param_desc, param_desc);
			dstr_cat(&n_param_desc, ".z");
			if (is_slider)
				obs_properties_add_float_slider(props, n_param_name.array,
					n_param_desc.array, f_min, f_max, f_step);
			else
				obs_properties_add_float(props, n_param_name.array,
					n_param_desc.array, f_min, f_max, f_step);

			if (param->type == GS_SHADER_PARAM_VEC3)
				break;

			dstr_copy(&n_param_name, param_name);
			dstr_cat(&n_param_name, ".w");
			dstr_copy(&n_param_desc, param_desc);
			dstr_cat(&n_param_desc, ".w");
			if (is_slider)
				obs_properties_add_float_slider(props, n_param_name.array,
					n_param_desc.array, f_min, f_max, f_step);
			else
				obs_properties_add_float(props, n_param_name.array,
					n_param_desc.array, f_min, f_max, f_step);

			break;
		case GS_SHADER_PARAM_TEXTURE:
			obs_properties_add_path(props, param_name, param_desc,
				OBS_PATH_FILE, shader_filter_texture_file_filter, NULL);
			break;
		}
		param->is_vec4 = is_vec4;
	}

	dstr_free(&shaders_path);

	return props;
}

static void shader_filter_update(void *data, obs_data_t *settings)
{
	struct shader_filter_data *filter = data;

	// Get expansions. Will be used in the video_tick() callback.
	filter->expand_left = (int)obs_data_get_int(settings, "expand_left");
	filter->expand_right = (int)obs_data_get_int(settings, "expand_right");
	filter->expand_top = (int)obs_data_get_int(settings, "expand_top");
	filter->expand_bottom = (int)obs_data_get_int(settings,
		"expand_bottom");

	if (filter->reload_effect)
	{
		filter->reload_effect = false;
		shader_filter_reload_effect(filter);
		obs_source_update_properties(filter->context);
	}

	size_t param_count = filter->stored_param_list.num;
	for (size_t param_index = 0; param_index < param_count; param_index++)
	{
		struct effect_param_data *param =
			(filter->stored_param_list.array + param_index);
		const char *param_name = param->name.array;

		switch (param->type)
		{
		case GS_SHADER_PARAM_BOOL:
			param->value.i = obs_data_get_bool(settings,
				param_name);
			break;
		case GS_SHADER_PARAM_FLOAT:
			param->value.f = obs_data_get_double(settings,
				param_name);
			break;
		case GS_SHADER_PARAM_INT:
			param->value.i = obs_data_get_int(settings,
				param_name);
			break;
		case GS_SHADER_PARAM_VEC4: // Assumed to be a color.
			/*todo: assume nothing, when annotations are around*/
			// Hack to ensure we have a default...
			obs_data_set_default_int(settings, param_name,
				0xff000000);

			param->value.i = obs_data_get_int(settings, param_name);
			break;
		case GS_SHADER_PARAM_TEXTURE:
			/*an assumption is made here that the param being
			used here is a plain old image...could easily be a
			source (something to do w/ annotations)*/
			if (param->image == NULL)
			{
				param->image = bzalloc(sizeof(gs_image_file_t));
			} else
			{
				obs_enter_graphics();
				gs_image_file_free(param->image);
				obs_leave_graphics();
			}

			gs_image_file_init(param->image,
				obs_data_get_string(settings, param_name));

			obs_enter_graphics();
			gs_image_file_init_texture(param->image);
			obs_leave_graphics();
			break;
		}
	}
}

static void shader_filter_tick(void *data, float seconds)
{
	struct shader_filter_data *filter = data;
	obs_source_t *target = obs_filter_get_target(filter->context);

	// Determine offsets from expansion values.
	int base_width = obs_source_get_base_width(target);
	int base_height = obs_source_get_base_height(target);

	filter->total_width = filter->expand_left
		+ base_width
		+ filter->expand_right;
	filter->total_height = filter->expand_top
		+ base_height
		+ filter->expand_bottom;

	filter->uv_scale.x = (float)filter->total_width / base_width;
	filter->uv_scale.y = (float)filter->total_height / base_height;

	filter->uv_offset.x = (float)(-filter->expand_left) / base_width;
	filter->uv_offset.y = (float)(-filter->expand_top) / base_height;

	filter->uv_pixel_interval.x = 1.0f / base_width;
	filter->uv_pixel_interval.y = 1.0f / base_height;

	filter->elapsed_time += seconds;

}

static void shader_filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	struct shader_filter_data *filter = data;

	if (filter->effect != NULL)
	{
		if (!obs_source_process_filter_begin(filter->context, GS_RGBA,
			OBS_NO_DIRECT_RENDERING))
		{
			return;
		}


		if (filter->param_uv_scale != NULL)
			gs_effect_set_vec2(filter->param_uv_scale,
				&filter->uv_scale);

		if (filter->param_uv_offset != NULL)
			gs_effect_set_vec2(filter->param_uv_offset,
				&filter->uv_offset);

		if (filter->param_uv_pixel_interval != NULL)
			gs_effect_set_vec2(filter->param_uv_pixel_interval,
				&filter->uv_pixel_interval);

		if (filter->param_elapsed_time != NULL)
			gs_effect_set_float(filter->param_elapsed_time,
				filter->elapsed_time);


		size_t param_count = filter->stored_param_list.num;
		for (size_t param_index = 0; param_index < param_count;
			param_index++)
		{
			struct effect_param_data *param = (param_index +
				filter->stored_param_list.array);
			struct vec4 color;

			switch (param->type)
			{
			case GS_SHADER_PARAM_BOOL:
				gs_effect_set_bool(param->param,
					param->value.i);
				break;
			case GS_SHADER_PARAM_FLOAT:
				gs_effect_set_float(param->param,
					(float)param->value.f);
				break;
			case GS_SHADER_PARAM_INT:
				gs_effect_set_int(param->param,
					(int)param->value.i);
				break;
			case GS_SHADER_PARAM_VEC4:
				/*assumption from earlier*, also something to
				change w/ annotations*/
				vec4_from_rgba(&color,
					(unsigned int)param->value.i);
				gs_effect_set_vec4(param->param, &color);
				break;
			case GS_SHADER_PARAM_TEXTURE:
				gs_effect_set_texture(param->param,
					(param->image ? param->image->texture
						: NULL));
				break;
			}
		}

		obs_source_process_filter_end(filter->context, filter->effect,
			filter->total_width, filter->total_height);
	}

}

static uint32_t shader_filter_getwidth(void *data)
{
	struct shader_filter_data *filter = data;

	return filter->total_width;
}

static uint32_t shader_filter_getheight(void *data)
{
	struct shader_filter_data *filter = data;

	return filter->total_height;
}

static void shader_filter_defaults(obs_data_t *settings)
{

}

struct obs_source_info shader_filter = {
	.id = "obs_shader_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.create = shader_filter_create,
	.destroy = shader_filter_destroy,
	.update = shader_filter_update,
	.video_tick = shader_filter_tick,
	.get_name = shader_filter_get_name,
	.get_defaults = shader_filter_defaults,
	.get_width = shader_filter_getwidth,
	.get_height = shader_filter_getheight,
	.video_render = shader_filter_render,
	.get_properties = shader_filter_properties
};

bool obs_module_load(void)
{	
	obs_register_source(&shader_filter);

	return true;
}

void obs_module_unload(void)
{
}
