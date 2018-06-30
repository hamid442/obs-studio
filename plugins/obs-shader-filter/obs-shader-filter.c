#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <util/base.h>
#include <util/dstr.h>
#include <util/platform.h>

#include <float.h>
#include <limits.h>
#include <stdio.h>

#include "tinyexpr.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs_shader_filter", "en-US")
#define blog(level, msg, ...) blog(level, "shader-filter: " msg, ##__VA_ARGS__)

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

double hlsl_clamp(double in, double min, double max) {
	if (in < min)
		return min;
	if (in > max)
		return max;
	return in;
}

#define M_PI_D 3.141592653589793238462643383279502884197169399375
double hlsl_degrees(double radians) {
	return radians * (180.0 / M_PI_D);
}

double hlsl_rad(double degrees) {
	return degrees * (M_PI_D / 180.0);
}

double float_max() {
	return FLT_MAX;
}

double float_min() {
	return FLT_MIN;
}

/* Additional likely to be used functions for mathmatical expressions */
void prep_te_funcs(struct darray *te_vars) {
	te_variable funcs[] = {
		{ "clamp", hlsl_clamp, TE_FUNCTION3 },
		{ "float_max", float_max, TE_FUNCTION0 },
		{ "float_min", float_min, TE_FUNCTION0 },
		{ "degrees", hlsl_degrees, TE_FUNCTION1 },
		{ "radians", hlsl_rad, TE_FUNCTION1 }
	};
	darray_push_back_array(sizeof(te_variable), te_vars, &funcs[0], 5);
}

void append_te_variable(struct darray *te_vars, te_variable *v) {
	darray_push_back(sizeof(te_variable), te_vars, v);
}

void clear_te_variables(struct darray *te_vars) {
	darray_free(te_vars);
}

/* Gets the integer value of an annotation (presuming its type is numeric) */
int get_annotation_int(gs_eparam_t *annotation, int default_value)
{
	struct gs_effect_param_info note_info;

	void* val = NULL;
	int ret = default_value;

	gs_effect_get_param_info(annotation, &note_info);

	if (annotation) {
		if (note_info.type == GS_SHADER_PARAM_FLOAT) {
			val = (void*)gs_effect_get_default_val(annotation);
			if (val) {
				ret = (int)*((float*)val);
				bfree(val);
				val = NULL;
			}
		} else if (note_info.type == GS_SHADER_PARAM_INT ||
			note_info.type == GS_SHADER_PARAM_BOOL) {
			val = (void*)gs_effect_get_default_val(annotation);
			if (val) {
				ret = *((int*)val);
				bfree(val);
				val = NULL;
			}
		}
	}

	return ret;
}

int get_eparam_int(gs_eparam_t *param, const char* name, int default_value)
{
	gs_eparam_t *note = gs_param_get_annotation_by_name(param, name);
	return get_annotation_int(note, default_value);
}

/* Boolean values are stored as integers (so...we cast) */
bool get_annotation_bool(gs_eparam_t *annotation, bool default_value)
{
	int val = get_annotation_int(annotation, (int)default_value);
	return val != 0;
}

/* Gets an annotion by name and returns its boolean value */
bool get_eparam_bool(gs_eparam_t *param, const char* name, bool default_value)
{
	gs_eparam_t *note = gs_param_get_annotation_by_name(param, name);
	return get_annotation_bool(note, default_value);
}

/* Gets the floating point value of an annotation */
float get_annotation_float(gs_eparam_t *annotation, float default_value)
{
	struct gs_effect_param_info note_info;

	void* val = NULL;
	float ret = default_value;

	gs_effect_get_param_info(annotation, &note_info);

	if (annotation) {
		if (note_info.type == GS_SHADER_PARAM_FLOAT) {
			val = (void*)gs_effect_get_default_val(annotation);
			if (val) {
				ret = *((float*)val);
				bfree(val);
				val = NULL;
			}
		} else if (note_info.type == GS_SHADER_PARAM_INT ||
			note_info.type == GS_SHADER_PARAM_BOOL) {
			val = (void*)gs_effect_get_default_val(annotation);
			if (val) {
				ret = (float)*((int*)val);
				bfree(val);
				val = NULL;
			}
		}
	}

	return ret;
}

/* Gets an annotion by name and returns its floating point value */
float get_eparam_float(gs_eparam_t *param, const char* name,
	float default_value)
{
	gs_eparam_t *note = gs_param_get_annotation_by_name(param, name);
	return get_annotation_float(note, default_value);
}

/* Free w/ bfree */
char *get_annotation_string(gs_eparam_t *annotation, const char* default_value)
{
	struct gs_effect_param_info note_info;

	char* val = NULL;
	struct dstr ret;
	dstr_init_copy(&ret, default_value);

	gs_effect_get_param_info(annotation, &note_info);

	if (annotation && note_info.type == GS_SHADER_PARAM_STRING) {
		val = (char*)gs_effect_get_default_val(annotation);
		if (val) {
			dstr_copy(&ret, val);
			bfree(val);
			val = NULL;
		}
	}

	return ret.array;
}

/* Free w/ bfree */
char *get_eparam_string(gs_eparam_t *param, const char* name,
	const char* default_value)
{
	gs_eparam_t *note = gs_param_get_annotation_by_name(param, name);
	return get_annotation_string(note, default_value);
}

/* Struct for dealing w/ integers like the vec4 */
struct long4 {
	union {
		struct {
			int32_t x, y, z, w;
		};
		int32_t ptr[4];
		__m128 m;
	};
};

/* Struct for dealing w/ converting vec2 floating points to doubles */
struct double2 {
	union
	{
		struct {
			double x, y;
		};
		double f[2];
	};
};

/* Space saving functions (adding properties happens a lot) */
void obs_properties_add_float_prop(obs_properties_t *props,
	const char *name, const char *desc, double min, double max,
	double step, bool is_slider)
{
	if (is_slider)
		obs_properties_add_float_slider(props, name, desc, min, max,
			step);
	else
		obs_properties_add_float(props, name, desc, min, max, step);
}

void obs_properties_add_int_prop(obs_properties_t *props,
	const char *name, const char *desc, int min, int max, int step,
	bool is_slider)
{
	if (is_slider)
		obs_properties_add_int_slider(props, name, desc, min, max,
			step);
	else
		obs_properties_add_int(props, name, desc, min, max, step);
}

void obs_properties_add_numerical_prop(obs_properties_t *props,
	const char *name, const char *desc, double min, double max, double step,
	bool is_slider, bool is_float)
{
	if (is_float)
		obs_properties_add_float_prop(props, name, desc, min, max,
			step, is_slider);
	else
		obs_properties_add_int_prop(props, name, desc, min, max,
			step, is_slider);
}

void obs_properties_add_vec_prop(obs_properties_t *props,
	const char *name, const char *desc, double min, double max,
	double step, bool is_slider, bool is_float, int vec_num)
{
	const char* mixin = "xyzw";
	struct dstr n_param_name;
	struct dstr n_param_desc;
	dstr_init(&n_param_name);
	dstr_init(&n_param_desc);
	int vec_count = vec_num <= 4 ? (vec_num >= 0 ? vec_num : 0) : 4;
	for (int i = 0; i < vec_count; i++) {
		dstr_copy(&n_param_name, name);
		dstr_cat(&n_param_name, ".");
		dstr_ncat(&n_param_name, mixin + i, 1);
		dstr_copy(&n_param_desc, desc);
		dstr_cat(&n_param_desc, ".");
		dstr_ncat(&n_param_desc, mixin + i, 1);

		obs_properties_add_numerical_prop(props,
			n_param_name.array, n_param_desc.array,
			min, max, step, is_slider, is_float);
	}
	dstr_free(&n_param_name);
	dstr_free(&n_param_desc);
}

void obs_properties_add_vec_array(obs_properties_t *props,
	const char *name, const char *desc, double min, double max,
	double step, bool is_slider, int vec_num)
{
	obs_properties_add_vec_prop(props, name, desc, min, max,
		step, is_slider, true, vec_num);
}

void obs_properties_add_int_array(obs_properties_t *props,
	const char *name, const char *desc, double min, double max,
	double step, bool is_slider, int vec_num)
{
	obs_properties_add_vec_prop(props, name, desc, min, max,
		step, is_slider, false, vec_num);
}

/*
* functions to extract lists from annotations
*	< [int|float|bool|string] list_item = ?; string list_item_?_name = "" >
*/
void fill_int_list(obs_property_t *p, gs_eparam_t *param)
{
	uint32_t notes_count = gs_param_get_num_annotations(param);
	struct gs_effect_param_info info;

	bool uses_module_text = get_eparam_bool(param, "list_module_text",
		false);

	char *c_tmp = NULL;
	int value = 0;

	struct dstr name_variable;
	struct dstr value_string;
	dstr_init(&name_variable);
	dstr_init(&value_string);
	dstr_ensure_capacity(&value_string, 21);
	for (uint32_t i = 0; i < notes_count; i++) {
		gs_eparam_t *note = gs_param_get_annotation_by_idx(param, i);
		gs_effect_get_param_info(note, &info);

		if (info.type == GS_SHADER_PARAM_INT
			|| info.type == GS_SHADER_PARAM_FLOAT
			|| info.type == GS_SHADER_PARAM_BOOL) {

			if (astrcmpi_n(info.name, "list_item", 9) == 0){
				dstr_copy(&name_variable, info.name);
				dstr_cat(&name_variable, "_name");

				value = get_annotation_int(note, 0);

				sprintf(value_string.array, "%d", value);

				c_tmp = get_eparam_string(param,
					name_variable.array,
					value_string.array);

				obs_property_list_add_int(p, uses_module_text ?
					_MT(c_tmp) : c_tmp, value);

				bfree(c_tmp);
			}
		}
	}
	dstr_free(&name_variable);
	dstr_free(&value_string);
}

void fill_float_list(obs_property_t *p, gs_eparam_t *param)
{
	uint32_t notes_count = gs_param_get_num_annotations(param);
	struct gs_effect_param_info info;

	bool uses_module_text = get_eparam_bool(param, "list_module_text",
		false);

	char *c_tmp = NULL;
	float value = 0;

	struct dstr name_variable;
	struct dstr value_string;
	dstr_init(&name_variable);
	dstr_init(&value_string);
	dstr_ensure_capacity(&value_string, 21);
	for (uint32_t i = 0; i < notes_count; i++) {
		gs_eparam_t *note = gs_param_get_annotation_by_idx(param, i);
		gs_effect_get_param_info(note, &info);

		if (info.type == GS_SHADER_PARAM_INT
			|| info.type == GS_SHADER_PARAM_FLOAT
			|| info.type == GS_SHADER_PARAM_BOOL) {

			if (astrcmpi_n(info.name, "list_item", 9) == 0) {
				dstr_copy(&name_variable, info.name);
				dstr_cat(&name_variable, "_name");

				value = get_annotation_float(note, 0);

				sprintf(value_string.array, "%f", value);

				c_tmp = get_eparam_string(param,
					name_variable.array,
					value_string.array);

				obs_property_list_add_float(p, uses_module_text
					? _MT(c_tmp) : c_tmp, value);

				bfree(c_tmp);
			}
		}
	}
	dstr_free(&name_variable);
	dstr_free(&value_string);
}

void fill_string_list(obs_property_t *p, gs_eparam_t *param)
{
	uint32_t notes_count = gs_param_get_num_annotations(param);
	struct gs_effect_param_info info;

	bool uses_module_text = get_eparam_bool(param, "list_module_text",
		false);

	char *c_tmp = NULL;
	char *value = NULL;

	struct dstr name_variable;
	struct dstr value_string;
	dstr_init(&name_variable);
	dstr_init(&value_string);
	for (uint32_t i = 0; i < notes_count; i++) {
		gs_eparam_t *note = gs_param_get_annotation_by_idx(param, i);
		gs_effect_get_param_info(note, &info);

		if (info.type == GS_SHADER_PARAM_STRING) {

			if (astrcmpi_n(info.name, "list_item_", 10) == 0) {
				dstr_copy(&name_variable, info.name);
				if (dstr_find(&name_variable, "_name"))
					continue;
				dstr_cat(&name_variable, "_name");

				value = get_annotation_string(note, "");
				dstr_copy(&value_string, value);

				c_tmp = get_eparam_string(param,
					name_variable.array,
					value_string.array);

				obs_property_list_add_string(p,
					uses_module_text ? _MT(c_tmp) : c_tmp,
					value);

				bfree(c_tmp);
				bfree(value);
			}
		}
	}
	dstr_free(&name_variable);
	dstr_free(&value_string);
}

/* functions to add sources to a list for use as textures */
bool fill_properties_source_list(void *param, obs_source_t *source)
{
	obs_property_t *p = (obs_property_t*)param;
	uint32_t flags = obs_source_get_output_flags(source);
	const char* source_name = obs_source_get_name(source);
	if ((flags & OBS_SOURCE_VIDEO) != 0 && obs_source_active(source)) {
		obs_property_list_add_string(p, source_name, source_name);
	}
	return true;
}

void fill_source_list(obs_property_t *p) {
	obs_enum_sources(&fill_properties_source_list, (void*)p);
}

int obs_get_vec_num(enum gs_shader_param_type type) {
	switch (type) {
	case GS_SHADER_PARAM_VEC4:
	case GS_SHADER_PARAM_INT4:
		return 4;
	case GS_SHADER_PARAM_VEC3:
	case GS_SHADER_PARAM_INT3:
		return 3;
	case GS_SHADER_PARAM_VEC2:
	case GS_SHADER_PARAM_INT2:
		return 2;
	case GS_SHADER_PARAM_FLOAT:
	case GS_SHADER_PARAM_INT:
		return 1;
	case GS_SHADER_PARAM_MATRIX4X4:
		return 16;
	}
	return 0;
}

struct effect_param_data
{
	/* The name and description of an obs property */
	struct dstr name;
	struct dstr desc;
	enum gs_shader_param_type type;
	gs_eparam_t *param;

	gs_image_file_t *image;

	bool is_vec4;
	bool is_list;
	bool is_source;
	bool is_media;

	bool bound;

	/* An array of strings for use w/ the array types */
	struct dstr array_names[4];

	obs_source_t *media_source;
	gs_texrender_t *texrender;

	/* These store the varieties of values passed to the shader */
	union
	{
		long long i;
		double f;
		struct vec4 v4;
		struct long4 l4;
	} value;

	/* These hold the above as doubles for use in expressions */
	union
	{
		double f[4];
	} te_bind;
};

void effect_param_data_release(struct effect_param_data *param) {
	dstr_free(&param->name);
	dstr_free(&param->desc);

	gs_texrender_destroy(param->texrender);
	param->texrender = NULL;

	obs_source_release(param->media_source);
	param->media_source = NULL;

	obs_enter_graphics();
	gs_image_file_free(param->image);
	obs_leave_graphics();

	bfree(param->image);
	param->image = NULL;
}

struct shader_filter_data
{
	obs_source_t *context;
	gs_effect_t *effect;

	bool reload_effect;
	struct dstr last_path;
	bool last_from_file;

	union {
		struct {
			struct dstr expr_left, expr_right, expr_top, expr_bottom;
		};
		struct dstr expr[4];
	};

	DARRAY(te_variable) vars;

	gs_eparam_t *param_uv_offset;
	gs_eparam_t *param_uv_scale;
	gs_eparam_t *param_uv_pixel_interval;
	gs_eparam_t *param_elapsed_time;

	union {
		struct {
			int expand_left, expand_right, expand_top, expand_bottom;
		};
		struct long4 expand;
	};

	union {
		struct {
			bool bind_left, bind_right, bind_top, bind_bottom;
		};
		bool bind[4];
	};

	bool show_expansions;

	int total_width;
	int total_height;

	struct vec2 uv_offset;
	struct vec2 uv_scale;
	struct vec2 uv_pixel_interval;

	struct double2 uv_scale_bind;
	struct double2 uv_pixel_interval_bind;
	
	float elapsed_time;

	union
	{
		double f;
	} elapsed_time_bind;

	obs_properties_t *props;

	DARRAY(struct effect_param_data) stored_param_list;
};

static void shader_filter_reload_effect(struct shader_filter_data *filter)
{
	obs_data_t *settings = obs_source_get_settings(filter->context);

	/* First, clean up the old effect and all references to it. */
	size_t param_count = filter->stored_param_list.num;
	for (size_t param_index = 0; param_index < param_count; param_index++)
	{
		struct effect_param_data *param =
			(filter->stored_param_list.array + param_index);
		effect_param_data_release(param);
	}

	da_free(filter->stored_param_list);
	/* clear expression variables, they need to be refreshed */
	da_free(filter->vars);

	filter->param_elapsed_time = NULL;
	filter->param_uv_offset = NULL;
	filter->param_uv_pixel_interval = NULL;
	filter->param_uv_scale = NULL;

	/* make sure the expressions aren't considered bound yet */
	filter->bind_left = false;
	filter->bind_right = false;
	filter->bind_top = false;
	filter->bind_bottom = false;

	if (filter->effect != NULL)
	{
		obs_enter_graphics();
		gs_effect_destroy(filter->effect);
		filter->effect = NULL;
		obs_leave_graphics();
	}

	/* Load text */
	const char *shader_text = NULL;

	const char *file_name = obs_data_get_string(settings,
		"shader_file_name");

	/* Load default effect text if no file is selected */
	if (file_name && file_name[0] != '\0')
		shader_text = os_quick_read_utf8_file(file_name);
	else
		shader_text = effect_template_default_image_shader;

	/* Load empty effect if file is empty / doesn't exist */
	if (shader_text == NULL)
		shader_text = "";

	struct dstr effect_text;
	dstr_init_copy(&effect_text, shader_text);

	/* Create the effect. */ 
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

	/* Prepare propoerties for mathmatical expressions to use */
	da_init(filter->vars);
	te_variable px_bind[] = {
		{ "elapsed_time", &filter->elapsed_time_bind.f },
		{ "uv_scale_x", &filter->uv_scale_bind.x },
		{ "uv_scale_y", &filter->uv_scale_bind.y },
		{ "uv_pixel_interval_x", &filter->uv_pixel_interval_bind.x},
		{ "uv_pixel_interval_y", &filter->uv_pixel_interval_bind.y}
	};

	darray_push_back_array(sizeof(te_variable), &filter->vars, &px_bind[0],
		5);

	/* Store references to the new effect's parameters. */
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

		else if (strcmp(info.name, "ViewProj") == 0)
			filter->show_expansions = get_eparam_bool(param,
				"show_expansions", false);

		else if	(strcmp(info.name, "image") == 0) {
			/* Nothing. */
		} else {
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
	for (size_t i = 0; i < 4; i++)
		dstr_init(&filter->expr[i]);

	dstr_copy(&filter->last_path, obs_data_get_string(settings,
		"shader_file_name"));

	filter->last_from_file = obs_data_get_bool(settings,
		"from_file");

	da_init(filter->stored_param_list);
	da_init(filter->vars);

	obs_source_update(source, settings);

	return filter;
}

static void shader_filter_destroy(void *data)
{
	struct shader_filter_data *filter = data;

	dstr_free(&filter->last_path);

	size_t i;
	for (i = 0; i < filter->stored_param_list.num; i++)
		effect_param_data_release(&filter->stored_param_list.array[i]);

	for (i = 0; i < 4; i++)
		dstr_free(&filter->expr[i]);

	da_free(filter->stored_param_list);
	da_free(filter->vars);

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

void set_expansion_bindings(gs_eparam_t *param, bool* bound_left,
	bool* bound_right, bool* bound_top, bool* bound_bottom)
{
	if (bound_left && !*bound_left &&
		get_eparam_bool(param, "bind_left", false))
	{
		*bound_left = true;
	}
	if (bound_right && !*bound_right && 
		get_eparam_bool(param, "bind_right", false))
	{
		*bound_right = true;
	}
	if (bound_top && !*bound_top &&
		get_eparam_bool(param, "bind_top", false))
	{
		*bound_top = true;
	}
	if (bound_bottom && !bound_bottom &&
		get_eparam_bool(param, "bind_bottom", false))
	{
		*bound_bottom = true;
	}
}

void set_expansion_bindings_vec(gs_eparam_t *param, bool* bound_left,
	bool* bound_right, bool* bound_top, bool* bound_bottom) {

	if (bound_left && !*bound_left && (
		get_eparam_bool(param, "bind_left_x", false) ||
		get_eparam_bool(param, "bind_left_y", false) ||
		get_eparam_bool(param, "bind_left_z", false) ||
		get_eparam_bool(param, "bind_left_w", false)))
		*bound_left = true;

	if (bound_right && !*bound_right && (
		get_eparam_bool(param, "bind_right_x", false) ||
		get_eparam_bool(param, "bind_right_y", false) ||
		get_eparam_bool(param, "bind_right_z", false) ||
		get_eparam_bool(param, "bind_right_w", false)))
		*bound_right = true;

	if (bound_top && !*bound_top && (
		get_eparam_bool(param, "bind_top_x", false) ||
		get_eparam_bool(param, "bind_top_y", false) ||
		get_eparam_bool(param, "bind_top_z", false) ||
		get_eparam_bool(param, "bind_top_w", false)))
		*bound_top = true;

	if (bound_bottom && !bound_bottom && (
		get_eparam_bool(param, "bind_bottom_x", false) ||
		get_eparam_bool(param, "bind_bottom_y", false) ||
		get_eparam_bool(param, "bind_bottom_z", false) ||
		get_eparam_bool(param, "bind_bottom_w", false)))

		*bound_bottom = true;
}

void prep_bind_value(bool *bound, int* binding, struct effect_param_data *param,
	const char* name, bool is_float, struct dstr *expr,
	struct darray *var_list) {

	if (bound && binding) {
		if (!*bound && get_eparam_bool(param->param, name, false)) {
			struct dstr formula_name;
			dstr_init_copy(&formula_name, name);
			dstr_cat(&formula_name, "_expr");

			char *expression = get_eparam_string(param->param,
				formula_name.array, param->name.array);
			dstr_free(&formula_name);

			dstr_free(expr);
			dstr_init_copy(expr, expression);

			*bound = true;
		}
		/* update values */
		const char* mixin = "xyzw";
		for (size_t i = 0; i < 4; i++) {
			dstr_free(&param->array_names[i]);
			dstr_init_copy_dstr(&param->array_names[i],
				&param->name);
			dstr_cat(&param->array_names[i], "_");
			dstr_ncat(&param->array_names[i], mixin + i, 1);
		}

		switch (param->type) {
		case GS_SHADER_PARAM_BOOL:
		case GS_SHADER_PARAM_INT:
			param->te_bind.f[0] = (double)param->value.i;
			break;
		case GS_SHADER_PARAM_FLOAT:
			param->te_bind.f[0] = (double)param->value.f;
			break;
		case GS_SHADER_PARAM_INT2:
		case GS_SHADER_PARAM_INT3:
		case GS_SHADER_PARAM_INT4:
			for (size_t i = 0; i < 4; i++)
				param->te_bind.f[i] =
					(double)param->value.l4.ptr[i];

			break;
		case GS_SHADER_PARAM_VEC2:
		case GS_SHADER_PARAM_VEC3:
		case GS_SHADER_PARAM_VEC4:
			for (size_t i = 0; i < 4; i++)
				param->te_bind.f[i] =
					(double)param->value.v4.ptr[i];

			break;
		}
		/* bind values */
		if (!param->bound) {
			te_variable var[4] = {0};
			bool bind_array = false;

			switch (param->type) {
			case GS_SHADER_PARAM_BOOL:
			case GS_SHADER_PARAM_FLOAT:
			case GS_SHADER_PARAM_INT:
				var[0].address = &param->te_bind.f[0];
				var[0].name = param->name.array;
				break;
			case GS_SHADER_PARAM_INT2:
			case GS_SHADER_PARAM_INT3:
			case GS_SHADER_PARAM_INT4:
			case GS_SHADER_PARAM_VEC2:
			case GS_SHADER_PARAM_VEC3:
			case GS_SHADER_PARAM_VEC4:
				for (size_t i = 0; i < 4; i++) {
					var[i].address = &param->te_bind.f[i];
					var[i].name =
						param->array_names[i].array;
				}

				bind_array = true;
				break;
			default:
				return;
			}

			if (var[0].name == NULL) {
				printf("%s", var[0].name);
			}

			if (bind_array) {
				for (size_t i = 0; i < 4; i++) {
					append_te_variable(var_list, &var[i]);
				}
			} else {
				append_te_variable(var_list, &var[0]);
			}

			param->bound = true;
		}
	}
	return;
}

void bind_compile(int* binding, te_variable *vars, const char* expression,
	int count) {

	if (expression && strcmp(expression, "x") != 0) {
		int err;
		te_expr *n = te_compile(expression, vars, count, &err);

		if (n) {
			*binding = (int)te_eval(n);
			te_free(n);
		} else {
			*binding = 0;
			blog(LOG_WARNING,
				"Error in expression: %.*s<< error here >>%s",
				err, expression, expression+err);
		}
	} else {
		*binding = 0;
	}
}

void prep_bind_values(bool *bound_left, bool *bound_right, bool *bound_top,
	bool *bound_bottom, struct effect_param_data *param,
	struct shader_filter_data *filter) {

	int vec_num = obs_get_vec_num(param->type);
	bool is_float = (param->type == GS_SHADER_PARAM_FLOAT ||
		param->type == GS_SHADER_PARAM_VEC2 ||
		param->type == GS_SHADER_PARAM_VEC3 ||
		param->type == GS_SHADER_PARAM_VEC4);
	bool *bounds[4] = { bound_left, bound_right, bound_top, bound_bottom };

	const char *bind_names[4] = {
		"bind_left", "bind_right", "bind_top", "bind_bottom"
	};
	const char *mixin = "xyzw";

	struct dstr bind_name;
	dstr_init(&bind_name);

	if (vec_num == 1) {
		for (size_t i = 0; i < 4; i++) {
			prep_bind_value(bounds[i], &filter->expand.ptr[i],
				param, bind_names[i], is_float,
				&filter->expr[i], &filter->vars.da);
		}
		return;
	}

	for (size_t j = 0; j < vec_num; j++) {
		for (size_t i = 1; i < 4; i++) {
			dstr_free(&bind_name);
			dstr_init_copy(&bind_name, bind_names[i]);
			dstr_cat(&bind_name, "_");
			dstr_ncat(&bind_name, mixin + j, 1);
			prep_bind_value(bounds[i], &filter->expand.ptr[i],
				param, bind_name.array, is_float,
				&filter->expr[i], &filter->vars.da);
		}
	}

	dstr_free(&bind_name);
}

void render_source(struct effect_param_data *param, float source_cx,
	float source_cy) {

	uint32_t media_cx = obs_source_get_width(param->media_source);
	uint32_t media_cy = obs_source_get_height(param->media_source);

	if (!media_cx || !media_cy)
		return;//break;

	float scale_x = source_cx / (float)media_cx;
	float scale_y = source_cy / (float)media_cy;

	gs_texrender_reset(param->texrender);
	if (gs_texrender_begin(param->texrender, media_cx, media_cy)) {
		struct vec4 clear_color;
		vec4_zero(&clear_color);

		gs_clear(GS_CLEAR_COLOR, &clear_color, 1, 0);
		gs_matrix_scale3f(scale_x, scale_y, 1.0f);
		obs_source_video_render(param->media_source);

		gs_texrender_end(param->texrender);
	} else {
		return;//break;
	}

	gs_texture_t *tex = gs_texrender_get_texture(param->texrender);
	gs_effect_set_texture(param->param, tex);
}

static const char *shader_filter_texture_file_filter =
"Textures (*.bmp *.tga *.png *.jpeg *.jpg *.gif);;";

static const char *shader_filter_media_file_filter =
"Video Files (*.mp4 *.ts *.mov *.wmv *.flv *.mkv *.avi *.gif *.webm);;";

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

	/* Add in hidden properties to control crop / expansion */
	obs_property_t *expand_left = obs_properties_add_int_slider(props,
		"expand_left", obs_module_text("ShaderFilter.ExpandLeft"),
		-9999, 9999, 1);
	obs_property_t *expand_right = obs_properties_add_int_slider(props,
		"expand_right", obs_module_text("ShaderFilter.ExpandRight"),
		-9999, 9999, 1);
	obs_property_t *expand_top = obs_properties_add_int_slider(props,
		"expand_top", obs_module_text("ShaderFilter.ExpandTop"),
		-9999, 9999, 1);
	obs_property_t *expand_bottom = obs_properties_add_int_slider(props,
		"expand_bottom", obs_module_text("ShaderFilter.ExpandBottom"),
		-9999, 9999, 1);

	obs_property_t *file_name = obs_properties_add_path(props,
		"shader_file_name",
		obs_module_text("ShaderFilter.ShaderFileName"), OBS_PATH_FILE,
		NULL, shaders_path.array);

	obs_property_set_modified_callback(file_name,
		shader_filter_file_name_changed);

	obs_property_t *p = NULL;
	size_t param_count = filter->stored_param_list.num;

	bool bound_right = false;
	bool bound_left = false;
	bool bound_top = false;
	bool bound_bottom = false;

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
		struct dstr n_param_name;
		struct dstr n_param_desc;
		dstr_init(&n_param_name);
		dstr_init(&n_param_desc);
		bool is_slider = false;
		bool is_vec4 = false;
		bool is_source = false;
		bool is_media = false;
		bool hide_descs = false;
		bool hide_all_descs = false;

		/* handles <...[int|float] min [=]...>*/
		f_min = get_eparam_float(param->param, "min", f_min);
		i_min = get_eparam_int(param->param, "min", i_min);

		/* handles <...[int|float] max [=]...>*/
		f_max = get_eparam_float(param->param, "max", f_max);
		i_max = get_eparam_int(param->param, "max", i_max);

		/* handles <...[int|float] step [=];...>*/
		f_step = get_eparam_float(param->param, "step", f_step);
		i_step = get_eparam_int(param->param, "step", i_step);

		/* handles <...bool module_text [= true|false];...>*/
		uses_module_text = get_eparam_bool(param->param, "module_text",
			false);

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

		hide_all_descs = get_eparam_bool(param->param,
			"hide_all_descs", false);
		hide_descs = get_eparam_bool(param->param, "hide_descs",
			false);
		obs_data_t *list_data = NULL;

		const char *param_desc = !hide_all_descs ? param->desc.array :
			NULL;

		int vec_num = 1;
		bool is_list = get_eparam_bool(param->param, "is_list",
			false);

		/*todo: control gui elements added via annotations*/
		switch (param->type)
		{
		case GS_SHADER_PARAM_BOOL:
			set_expansion_bindings(param->param, &bound_left,
				&bound_right, &bound_top, &bound_bottom);

			if (is_list) {
				p = obs_properties_add_list(props, param_name,
					param_desc, OBS_COMBO_TYPE_LIST,
					OBS_COMBO_FORMAT_INT);

				i_tmp = get_eparam_bool(param->param,
					"enabled_module_text", false);
				c_tmp = get_eparam_string(param->param,
					"enabled_string", "On");
				obs_property_list_add_int(p, i_tmp ?
					_MT(c_tmp) : c_tmp, 1);
				bfree(c_tmp);

				i_tmp = get_eparam_bool(param->param,
					"disabled_module_text", false);
				c_tmp = get_eparam_string(param->param,
					"disabled_string", "Off");
				obs_property_list_add_int(p, i_tmp ?
					_MT(c_tmp) : c_tmp, 0);
				bfree(c_tmp);
			} else {
				obs_properties_add_bool(props, param_name,
					param_desc);
			}
			break;
		case GS_SHADER_PARAM_FLOAT:
		case GS_SHADER_PARAM_INT:
			set_expansion_bindings(param->param, &bound_left,
				&bound_right, &bound_top, &bound_bottom);

			note = gs_param_get_annotation_by_name(param->param,
				"is_slider");

			gs_effect_get_param_info(note, &note_info);

			bool is_float = param->type == GS_SHADER_PARAM_FLOAT;
			is_slider = get_eparam_bool(param->param, "is_slider",
				false);

			if (is_list) {
				p = obs_properties_add_list(props,
					param_name, param_desc,
					OBS_COMBO_TYPE_LIST,
					is_float ? OBS_COMBO_FORMAT_FLOAT :
					OBS_COMBO_FORMAT_INT);

				if (is_float)
					fill_float_list(p, param->param);
				else
					fill_int_list(p, param->param);
			} else {
				if (is_float)
					obs_properties_add_float_prop(props,
						param_name, param_desc, f_min,
						f_max, f_step, is_slider);
				else
					obs_properties_add_int_prop(props,
						param_name, param_desc, i_min,
						i_max, i_step, is_slider);
			}

			break;
		case GS_SHADER_PARAM_INT2:
		case GS_SHADER_PARAM_INT3:
		case GS_SHADER_PARAM_INT4:
			set_expansion_bindings_vec(param->param, &bound_left,
				&bound_right, &bound_top, &bound_bottom);

			is_slider = get_eparam_bool(param->param, "is_slider",
				false);

			vec_num = obs_get_vec_num(param->type);

			obs_properties_add_int_array(props,
				param_name, param_desc, i_min, i_max, i_step,
				is_slider, vec_num);

			break;
		case GS_SHADER_PARAM_VEC2:
		case GS_SHADER_PARAM_VEC3:
		case GS_SHADER_PARAM_VEC4:
			set_expansion_bindings_vec(param->param, &bound_left,
				&bound_right, &bound_top, &bound_bottom);

			is_vec4 = param->type == GS_SHADER_PARAM_VEC4 &&
				get_eparam_bool(param->param, "is_vec4",
					false);

			if (!is_vec4 && param->type == GS_SHADER_PARAM_VEC4) {
				obs_properties_add_color(props, param_name,
					param_desc);
				break;
			}
			is_slider = get_eparam_bool(param->param, "is_slider",
				false);

			vec_num = obs_get_vec_num(param->type);

			obs_properties_add_vec_array(props,
				param_name, param_desc, f_min, f_max, f_step,
				is_slider, vec_num);

			break;
		case GS_SHADER_PARAM_TEXTURE:
			is_source = get_eparam_bool(param->param, "is_source",
				false);
			if (is_source) {
				p = obs_properties_add_list(props,
					param_name, param_desc,
					OBS_COMBO_TYPE_LIST,
					OBS_COMBO_FORMAT_STRING);
				fill_source_list(p);
				break;
			}
			is_media = get_eparam_bool(param->param, "is_media",
				false);
			if (is_media) {
				obs_properties_add_path(props, param_name,
					param_desc, OBS_PATH_FILE,
					shader_filter_media_file_filter, NULL);
				break;
			}

			obs_properties_add_path(props, param_name, param_desc,
				OBS_PATH_FILE,
				shader_filter_texture_file_filter, NULL);
			break;
		}

		param->is_vec4 = is_vec4;
		param->is_list = is_list;
		param->is_source = is_source;
		param->is_media = is_media;

		dstr_free(&n_param_name);
		dstr_free(&n_param_desc);
	}

	obs_property_set_visible(expand_left,
		!bound_left && filter->show_expansions);

	obs_property_set_visible(expand_right,
		!bound_right && filter->show_expansions);

	obs_property_set_visible(expand_top,
		!bound_top && filter->show_expansions);

	obs_property_set_visible(expand_bottom,
		!bound_bottom && filter->show_expansions);

	dstr_free(&shaders_path);

	filter->props = props;

	return props;
}

/* obs_property_list_type() */

static void shader_filter_update(void *data, obs_data_t *settings)
{
	struct shader_filter_data *filter = data;
	
	if (filter->reload_effect)
	{
		filter->reload_effect = false;

		prep_te_funcs(&filter->vars);
		shader_filter_reload_effect(filter);
		obs_source_update_properties(filter->context);
	}

	const char* mixin = "xyzw";
	size_t param_count = filter->stored_param_list.num;
	for (size_t param_index = 0; param_index < param_count; param_index++)
	{
		struct effect_param_data *param =
			(filter->stored_param_list.array + param_index);
		const char *param_name = param->name.array;
		obs_property_t *prop = obs_properties_get(filter->props,
			param_name);

		/* get the property names (if this was meant to be an array) */
		for (size_t i = 0; i < 4; i++) {
			dstr_free(&param->array_names[i]);
			dstr_init_copy(&param->array_names[i], param_name);
			dstr_cat(&param->array_names[i], ".");
			dstr_ncat(&param->array_names[i], mixin + i, 1);
		}

		int vec_num;

		/* assign the value of the parameter from the properties */
		/* we take advantage of doing this step to "cache" values */
		switch (param->type)
		{
		case GS_SHADER_PARAM_BOOL:
			if (param->is_list)
				param->value.i = obs_data_get_int(settings,
					param_name);
			else
				param->value.i = obs_data_get_bool(settings,
					param_name);
			break;
		case GS_SHADER_PARAM_FLOAT:
			param->value.f = obs_data_get_double(settings,
				param_name);

			prep_bind_values(&filter->bind_left,
				&filter->bind_right, &filter->bind_top,
				&filter->bind_bottom, param, filter);

			break;
		case GS_SHADER_PARAM_INT:
			param->value.i = obs_data_get_int(settings,
				param_name);

			prep_bind_values(&filter->bind_left,
				&filter->bind_right, &filter->bind_top,
				&filter->bind_bottom, param, filter);

			break;
		case GS_SHADER_PARAM_INT2:
		case GS_SHADER_PARAM_INT3:
		case GS_SHADER_PARAM_INT4:
			vec_num = obs_get_vec_num(param->type);
			for (size_t i = 0; i < vec_num; i++) {
				param->value.l4.ptr[i] = obs_data_get_int(
					settings, param->array_names[i].array);
			}

			prep_bind_values(&filter->bind_left,
				&filter->bind_right, &filter->bind_top,
				&filter->bind_bottom, param, filter);

			break;
		case GS_SHADER_PARAM_VEC2:
		case GS_SHADER_PARAM_VEC3:
			vec_num = obs_get_vec_num(param->type);
			for (size_t i = 0; i < vec_num; i++) {
				param->value.v4.ptr[i] =
					(float)obs_data_get_double(
					settings,
						param->array_names[i].array);
			}

			prep_bind_values(&filter->bind_left,
				&filter->bind_right, &filter->bind_top,
				&filter->bind_bottom, param, filter);

			break;
		case GS_SHADER_PARAM_VEC4:
			param->is_vec4 = get_eparam_bool(param->param,
				"is_vec4", false);

			if (param->is_vec4) {
				vec_num = obs_get_vec_num(param->type);
				for (size_t i = 0; i < vec_num; i++) {
					param->value.v4.ptr[i] =
						(float)obs_data_get_double(
						settings,
						param->array_names[i].array);
				}

				prep_bind_values(&filter->bind_left,
					&filter->bind_right, &filter->bind_top,
					&filter->bind_bottom, param, filter);

			} else {
				obs_data_set_default_int(settings, param_name,
					0xff000000);

				param->value.i = obs_data_get_int(settings,
					param_name);
			}
			break;
		case GS_SHADER_PARAM_TEXTURE:
			param->is_source = get_eparam_bool(param->param,
				"is_source", false);
			param->is_media = get_eparam_bool(param->param,
				"is_media", false);

			if (param->is_source) {
				if (!param->texrender)
					param->texrender = gs_texrender_create(
						GS_RGBA, GS_ZS_NONE);

				obs_source_release(param->media_source);
				param->media_source = obs_get_source_by_name(
					obs_data_get_string(settings,
						param_name)
				);
				break;
			} else if (param->is_media) {
				if(!param->texrender)
					param->texrender = gs_texrender_create(
						GS_RGBA, GS_ZS_NONE);
				const char *path = obs_data_get_string(
					settings, param_name);

				obs_data_t *media_settings = obs_data_create();
				obs_data_set_string(media_settings,
					"local_file", path);

				obs_source_release(param->media_source);
				param->media_source =
					obs_source_create_private(
						"ffmpeg_source", NULL,
						media_settings);

				obs_data_release(media_settings);
				break;
			} else {
				if (param->image == NULL)
				{
					param->image = bzalloc(
						sizeof(gs_image_file_t));
				} else {
					obs_enter_graphics();
					gs_image_file_free(param->image);
					obs_leave_graphics();
				}
				gs_image_file_init(param->image,
					obs_data_get_string(settings,
						param_name));

				obs_enter_graphics();
				gs_image_file_init_texture(param->image);
				obs_leave_graphics();
			}
			break;
		}
	}

	/* Calculate the stretch of the size of the source via expression */
	for (size_t i = 0; i < 4; i++) {
		bind_compile(&filter->expand.ptr[i], &filter->vars.array[0],
			filter->expr[i].array, filter->vars.num);
	}

	/* Calculate expansions if not already set. */
	/* Will be used in the video_tick() callback. */
	if (!filter->bind_left)
		filter->expand_left = (int)obs_data_get_int(settings,
			"expand_left");
		
	if (!filter->bind_right)
		filter->expand_right = (int)obs_data_get_int(settings,
			"expand_right");

	if (!filter->bind_top)
		filter->expand_top = (int)obs_data_get_int(settings,
			"expand_top");

	if (!filter->bind_bottom)
		filter->expand_bottom = (int)obs_data_get_int(settings,
			"expand_bottom");
}

static void shader_filter_tick(void *data, float seconds)
{
	struct shader_filter_data *filter = data;
	obs_source_t *target = obs_filter_get_target(filter->context);

	/* Determine offsets from expansion values. */
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

	filter->uv_scale_bind.x = filter->uv_scale.x;
	filter->uv_scale_bind.y = filter->uv_scale.y;

	filter->uv_offset.x = (float)(-filter->expand_left) / base_width;
	filter->uv_offset.y = (float)(-filter->expand_top) / base_height;

	filter->uv_pixel_interval.x = 1.0f / base_width;
	filter->uv_pixel_interval.y = 1.0f / base_height;

	filter->uv_pixel_interval_bind.x = filter->uv_pixel_interval.x;
	filter->uv_pixel_interval_bind.y = filter->uv_pixel_interval.y;

	filter->elapsed_time += seconds;
	filter->elapsed_time_bind.f += seconds;

}

static void shader_filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	struct shader_filter_data *filter = data;

	if (filter->effect != NULL)
	{
		if (!obs_source_process_filter_begin(filter->context, GS_RGBA,
			OBS_NO_DIRECT_RENDERING))
			return;

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

		float source_cx = (float)obs_source_get_width(filter->context);
		float source_cy = (float)obs_source_get_height(filter->context);

		size_t param_count = filter->stored_param_list.num;
		for (size_t param_index = 0; param_index < param_count;
			param_index++)
		{
			struct effect_param_data *param = (param_index +
				filter->stored_param_list.array);
			struct vec4 color;

			switch (param->type)
			{
			case GS_SHADER_PARAM_FLOAT:
				gs_effect_set_float(param->param,
					(float)param->value.f);
				break;
			case GS_SHADER_PARAM_BOOL:
			case GS_SHADER_PARAM_INT:
				gs_effect_set_int(param->param,
					(int)param->value.i);
				break;
			case GS_SHADER_PARAM_INT2:
				gs_effect_set_vec2(param->param,
					&param->value.l4);
				break;
			case GS_SHADER_PARAM_INT3:
				gs_effect_set_vec3(param->param,
					&param->value.l4);
				break;
			case GS_SHADER_PARAM_INT4:
				gs_effect_set_vec4(param->param,
					&param->value.l4);
				break;
			case GS_SHADER_PARAM_VEC2:
				gs_effect_set_vec2(param->param,
					&param->value.v4);
				break;
			case GS_SHADER_PARAM_VEC3:
				gs_effect_set_vec3(param->param,
					&param->value.v4);
				break;
			case GS_SHADER_PARAM_VEC4:
				/* Treat as color or vec4 */
				if (param->is_vec4) {
					gs_effect_set_vec4(param->param,
						&param->value.v4);
				} else {
					vec4_from_rgba(&color,
						(unsigned int)param->value.i);
					gs_effect_set_vec4(param->param,
						&color);
				}
				break;
			case GS_SHADER_PARAM_TEXTURE:
				/* Render texture from a source */
				if (param->is_source || param->is_media) {
					render_source(param, source_cx,
						source_cy);

					break;
				}
				/* Otherwise use image file as texture */
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
