#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <graphics/matrix4.h>
#ifdef __cplusplus
}
#endif

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
#include <unordered_map>
#include <vector>
#include <list>

#include "fft.h"
#include "tinyexpr.h"
#include "mtrandom.h"

#define _OMT obs_module_text

struct in_shader_data {
	union {
		double   d;
		uint64_t u64i;
		int64_t  s64i;
		float    f;
		uint32_t u32i;
		int32_t  s32i;
		uint16_t u16i;
		int16_t  s16i;
		uint8_t  u8i;
		int8_t   s8i;
	};

	in_shader_data &operator=(const double &rhs)
	{
		d = rhs;
		return *this;
	}

	operator double() const
	{
		return d;
	}
};

struct out_shader_data {
	union {
		float    f;
		uint32_t u32i;
		int32_t  s32i;
		uint16_t u16i;
		int16_t  s16i;
		uint8_t  u8i;
		int8_t   s8i;
	};

	out_shader_data &operator=(const float &rhs)
	{
		f = rhs;
		return *this;
	}

	operator float() const
	{
		return f;
	}

	operator uint32_t() const
	{
		return u32i;
	}

	operator int32_t() const
	{
		return s32i;
	}
};

struct bind2 {
	union {
		in_shader_data x, y;
		double         ptr[2];
	};

	bind2 &operator=(const vec2 &rhs)
	{
		x = rhs.x;
		y = rhs.y;
		return *this;
	}
};

class TinyExpr : public std::vector<te_variable> {
	std::string _expr;
	te_expr *   _compiled  = nullptr;
	int         _err       = 0;
	std::string _errString = "";

public:
	TinyExpr(){};
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
	template<class DataType> DataType evaluate(DataType default_value = 0)
	{
		DataType ret = default_value;
		if (_compiled)
			ret = (DataType)te_eval(_compiled);
		return ret;
	};
	void compile(std::string expression)
	{
		if (expression.empty())
			return;
		if (_compiled)
			releaseExpression();
		_compiled = te_compile(expression.c_str(), data(), (int)size(), &_err);
		if (!_compiled) {
			_errString = "Expression Error At [" + std::to_string(_err) + "]:\n" +
					expression.substr(0, _err) + "[ERROR HERE]" + expression.substr(_err);
			blog(LOG_WARNING, _errString.c_str());
		} else {
			_errString = "";
			_expr      = expression;
		}
	};
	bool success()
	{
		return _err == 0;
	}
	std::string errorString()
	{
		return _errString;
	}
	operator bool()
	{
		return success();
	}
};

class PThreadMutex {
	bool            _mutexCreated;
	pthread_mutex_t _mutex;

public:
	PThreadMutex(int PTHREAD_MUTEX_TYPE = PTHREAD_MUTEX_RECURSIVE)
	{
		pthread_mutexattr_t attr;
		if (pthread_mutexattr_init(&attr) != 0) {
			_mutexCreated = false;
			return;
		}
		if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_TYPE) != 0) {
			_mutexCreated = false;
			return;
		}
		if (pthread_mutex_init(&_mutex, &attr) != 0)
			_mutexCreated = false;
		else
			_mutexCreated = true;
	}
	~PThreadMutex()
	{
		if (_mutexCreated)
			pthread_mutex_destroy(&_mutex);
	}
	void lock()
	{
		if (_mutexCreated)
			pthread_mutex_lock(&_mutex);
	}
	void unlock()
	{
		if (_mutexCreated)
			pthread_mutex_unlock(&_mutex);
	}
};

class EVal;
class EParam;
class ShaderFilter;
class ShaderData;

class ShaderParameter {
protected:
	EParam *    _param = nullptr;
	std::string _name;
	std::string _description;

	PThreadMutex *_mutex = nullptr;

	gs_shader_param_type _paramType;

	ShaderData *    _shaderData = nullptr;
	obs_property_t *_property   = nullptr;
	ShaderFilter *  _filter     = nullptr;

public:
	ShaderParameter(gs_eparam_t *param, ShaderFilter *filter);
	~ShaderParameter();

	void init(gs_shader_param_type paramType);

	std::string getName();
	std::string getDescription();
	EParam *    getParameter();
	gs_shader_param_type getParameterType();

	void lock();
	void unlock();
	void videoTick(ShaderFilter *filter, float elapsed_time, float seconds);
	void videoRender(ShaderFilter *filter);
	void update(ShaderFilter *filter);
	void getProperties(ShaderFilter *filter, obs_properties_t *props);
	void onPass(ShaderFilter *filter, const char *technique, size_t pass, gs_texture_t *texture);
	void onTechniqueEnd(ShaderFilter *filter, const char *technique, gs_texture_t *texture);
};

class ShaderFilter {
protected:
	uint32_t total_width;
	uint32_t total_height;

	std::string _effect_path;
	std::string _effect_string;

	gs_effect_t *effect    = nullptr;
	obs_data_t * _settings = nullptr;

	PThreadMutex *_mutex         = nullptr;
	bool          _reload_effect = true;

	TinyExpr expression;
public:
	gs_texrender_t *filter_texrender = nullptr;

	double _clickCount;
	double _mouseUp;
	double _mouseType;
	double _mouseX;
	double _mouseY;
	double _mouseClickX;
	double _mouseClickY;
	double _mouseLeave;
	double _mouseWheelX;
	double _mouseWheelY;
	double _mouseWheelDeltaX;
	double _mouseWheelDeltaY;

	double _keyModifiers;
	double _keyUp;
	double _nativeKeyModifiers;
	double _key;

	double _focus;

	std::vector<ShaderParameter *>                     paramList = {};
	std::unordered_map<std::string, ShaderParameter *> paramMap;
	std::vector<ShaderParameter *>                     evaluationList = {};

	std::string resizeExpressions[4];
	int         resizeLeft   = 0;
	int         resizeRight  = 0;
	int         resizeTop    = 0;
	int         resizeBottom = 0;

	int baseWidth = 0;
	int baseHeight = 0;

	float          elapsedTime        = 0;
	in_shader_data elapsedTimeBinding = {0};

	std::vector<std::pair<gs_eparam_t *, float *>> float_pairs;

	vec2 uvScale;
	vec2 uvOffset;
	vec2 uvPixelInterval;

	bind2 uvScaleBinding;
	bind2 uvOffsetBinding;
	bind2 uvPixelIntervalBinding;

	std::vector<std::pair<gs_eparam_t *, vec2 *>> vec2_pairs;

	matrix4                                          view_proj;
	std::vector<std::pair<gs_eparam_t *, matrix4 *>> matrix4_pairs;

	obs_source_t *context = nullptr;

	obs_data_t *                   getSettings();
	std::string                    getPath();
	void                           setPath(std::string path);
	void                           prepReload();
	bool                           needsReloading();
	std::vector<ShaderParameter *> parameters();
	void                           clearExpression();
	void                           appendVariable(te_variable var);

	void compileExpression(std::string expresion = "");

	template<class DataType> DataType evaluateExpression(DataType default_value = 0);
	bool                              expressionCompiled();
	std::string                       expressionError();

	ShaderFilter(obs_data_t *settings, obs_source_t *source);
	~ShaderFilter();

	void lock();
	void unlock();

	uint32_t getWidth();
	uint32_t getHeight();

	void updateCache(gs_eparam_t *param);
	void reload();

	static void *            create(obs_data_t *settings, obs_source_t *source);
	static void              destroy(void *data);
	static const char *      getName(void *unused);
	static void              videoTick(void *data, float seconds);
	static void              videoTickSource(void *data, float seconds);
	static void              videoRender(void *data, gs_effect_t *effect);
	static void              videoRenderSource(void *data, gs_effect_t *effect);
	static void              update(void *data, obs_data_t *settings);
	static obs_properties_t *getProperties(void *data);
	static obs_properties_t *getPropertiesSource(void *data);
	static uint32_t          getWidth(void *data);
	static uint32_t          getHeight(void *data);
	static void              getDefaults(obs_data_t *settings);
	static void mouseClick(void *data, const struct obs_mouse_event *event, int32_t type, bool mouse_up,
			uint32_t click_count);
	static void mouseMove(void *data, const struct obs_mouse_event *event, bool mouse_leave);
	static void mouseWheel(void *data, const struct obs_mouse_event *event, int x_delta, int y_delta);
	static void focus(void *data, bool focus);
	static void keyClick(void *data, const struct obs_key_event *event, bool key_up);
};