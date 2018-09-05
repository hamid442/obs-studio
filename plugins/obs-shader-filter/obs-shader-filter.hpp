#pragma once

#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <graphics/matrix4.h>
#include <obs-module.h>
#include <util/base.h>
#include <util/circlebuf.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <util/threading.h>

#include <float.h>
#include <limits.h>
#include <stdio.h>

#include <string>
#include <vector>
#include <list>

#include "fft.h"
#include "tinyexpr.h"
#include "mtrandom.h"

#define _MT obs_module_text

struct in_shader_data {
	union {
		double d;
		uint64_t u64i;
		int64_t s64i;
		float f;
		uint32_t u32i;
		int32_t s32i;
		uint16_t u16i;
		int16_t s16i;
		uint8_t u8i;
		int8_t s8i;
	};
};

struct out_shader_data {
	union {
		float f;
		uint32_t u32i;
		int32_t s32i;
		uint16_t u16i;
		int16_t s16i;
		uint8_t u8i;
		int8_t s8i;
	};
};

static 	enum ShaderParameterType {
	unspecified,
	slider,
	list,
	media,
	source,
	audio_waveform,
	audio_fft,
	audio_power_spectrum
};

class TinyExpr : public std::vector<te_variable> {
	std::string _expr;
	te_expr *_compiled = nullptr;
public:
	TinyExpr()
	{
	};
	~TinyExpr()
	{
		releaseExpression();
	};
	void releaseExpression()
	{
		if (_compiled) {
			te_free(_compiled);
			_compiled = nullptr;
		}
	};
	template <class DataType>
	DataType evaluate(DataType default_value = 0)
	{
		DataType ret = default_value;
		if (_compiled)
			ret = te_eval(_compiled);
		return ret;
	};

	void compile(std::string expression)
	{
		if (expression.empty())
			return;
		if (_compiled)
			releaseExpression();
		int e;
		_compiled = te_compile(expression.c_str(), data(), size(), &e);
		if (!_compiled) {
			blog(LOG_WARNING, "Expression Error At [%i]:\n%.*s[Error Here]",
					e, e, expression.c_str(),
					expression.c_str() + e);
		} else {
			_expr = expression;
		}
	};
};

class EVal;
class EParam;
class ShaderFilter;
class ShaderData;

class ShaderParameter {
protected:
	EParam *_param;
	std::string _name;
	std::string _description;

	pthread_mutex_t _mutex;
	bool _mutex_created = false;

	ShaderParameterType _type;
	gs_shader_param_type _paramType;

	ShaderData *_shaderData = nullptr;
	obs_property_t *_property;
	ShaderFilter *_filter;
public:
	ShaderParameter(gs_eparam_t *param, ShaderFilter *filter);
	~ShaderParameter();

	void init(ShaderParameterType type, gs_shader_param_type paramType);

	std::string getName();
	std::string getDescription();
	EParam *getParameter();

	void lock();
	void unlock();
	void setData();
	template <class DataType>
	void setData(DataType t);
	template <class DataType>
	void setData(std::vector<DataType> t);
	void videoTick(ShaderFilter *filter, float elapsed_time, float seconds);
	void videoRender(ShaderFilter *filter);
	void update(ShaderFilter *filter);
	void getProperties(ShaderFilter *filter, obs_properties_t *props);

};

class ShaderFilter {
protected:
	uint32_t total_width;
	uint32_t total_height;

	std::vector<ShaderParameter*> paramList;
	std::vector<ShaderParameter*> evaluationList;

	std::string _effect_path;
	std::string _effect_string;

	gs_effect_t *effect = nullptr;
	obs_data_t *_settings = nullptr;

	pthread_mutex_t _mutex;
	bool _mutex_created = false;
	bool _reload_effect = true;

	TinyExpr expression;
public:
	int resize_left = 0;
	int resize_right = 0;
	int resize_top = 0;
	int resize_bottom = 0;

	float elapsedTime = 0;
	double elapsedTimeBinding = 0;
	std::vector<std::pair<gs_eparam_t*, float*>> float_pairs;

	vec2 uv_scale;
	vec2 uv_offset;
	vec2 uv_pixel_interval;
	std::vector<std::pair<gs_eparam_t*, vec2*>> vec2_pairs;

	matrix4 view_proj;
	std::vector<std::pair<gs_eparam_t*, matrix4*>> matrix4_pairs;

	obs_source_t *context = nullptr;

	obs_data_t *getSettings();
	std::string getPath();
	void setPath(std::string path);
	void prepReload();
	bool needsReloading();
	std::vector<ShaderParameter*> parameters();
	void clearExpression();
	void appendVariable(te_variable var);

	void compileExpression(std::string expresion = "");

	template <class DataType>
	DataType evaluateExpression(DataType default_value = 0);

	ShaderFilter(obs_data_t *settings, obs_source_t *source);
	~ShaderFilter();

	void lock();
	void unlock();

	uint32_t getWidth();
	uint32_t getHeight();

	void updateCache(gs_eparam_t *param);
	void reload();

	static void *create(obs_data_t *settings, obs_source_t *source);
	static void destroy(void *data);
	static const char *getName(void *unused);
	static void videoTick(void *data, float seconds);
	static void videoRender(void *data, gs_effect_t *effect);
	static void update(void *data, obs_data_t *settings);
	static obs_properties_t *getProperties(void *data);
	static uint32_t getWidth(void *data);
	static uint32_t getHeight(void *data);
	static void getDefaults(obs_data_t *settings);
};