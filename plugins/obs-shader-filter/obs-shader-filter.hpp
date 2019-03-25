#pragma once
#define _ENABLE_EXTENDED_ALIGNED_STORAGE

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
#include <algorithm>

#include "fft.h"
#include "tinyexpr.h"
#include "mtrandom.h"
#include "muparser/muParser.h"

#undef _ENABLE_EXTENDED_ALIGNED_STORAGE

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

class TinyExpr : public std::vector<te_variable> {
	std::string _expr;
	mu::Parser *_compiledMu = nullptr;
	int         _err = 0;
	std::string _errString = "";

	std::unordered_map<std::string, mu::Parser*> _compiledMuMap;
	std::unordered_map<std::string, int> _errMap;
	std::unordered_map<std::string, std::string> _errStrMap;
public:
	TinyExpr()
	{
	}
	~TinyExpr()
	{
		releaseExpression();
	}
	void releaseExpression()
	{
		for (auto it : _compiledMuMap) {
			delete it.second;
			it.second = nullptr;
		}
		_compiledMuMap.clear();
		_errMap.clear();
		_errStrMap.clear();
	}

	bool hasVariable(std::string search)
	{
		if (!size())
			return false;
		te_variable searchVar = { 0 };
		searchVar.name = search.c_str();
		return std::binary_search(begin(), end(), searchVar, [](te_variable a, te_variable b) {
			return strcmp(a.name, b.name) < 0;
		});
	}
	template<class DataType> DataType evaluate(DataType default_value = 0)
	{
		try {
			return (DataType)_compiledMu->Eval();
		} catch (mu::Parser::exception_type &) {
			return default_value;
		}
	}
	template<class DataType> DataType evaluate(std::string expression, DataType default_value = 0)
	{
		try {
			if (_compiledMuMap.count(expression))
				return (DataType)_compiledMu->Eval();
			return default_value;
		} catch (mu::Parser::exception_type &) {
			return default_value;
		}
	}
	void compile(std::string expression)
	{
		if (expression.empty())
			return;

		if (_compiledMuMap.count(expression)) {
			_errString = _errStrMap.at(expression);
			_err = _errMap.at(expression);
			_compiledMu = _compiledMuMap.at(expression);
			return;
		}

		blog(LOG_DEBUG, "First Compilation: %s", expression.c_str());
		_compiledMu = new mu::Parser();
		std::string name;

		for (size_t i = 0; i < size(); i++) {
			te_variable *a = &data()[i];
			int type = a->type & (~TE_FLAG_PURE);
			bool pure = a->type & (TE_FLAG_PURE);
#ifdef UNICODE
			name = a->name;
			mu::string_type n;
			n.reserve(name.size());
			os_utf8_to_wcs(name.c_str(), name.size(), &n[0], name.size());
			n = n.c_str();
#else
			name = a->name;
			mu::string_type n = name;
#endif
			try {
				switch (type) {
				case TE_CONSTANT:
					_compiledMu->DefineConst(n, (*(mu::value_type*)(&a->address)));
					break;
				case TE_VARIABLE:
					_compiledMu->DefineVar(n, (mu::value_type*)a->address);
					break;
				case TE_FUNCTION0:
					_compiledMu->DefineFun(n, reinterpret_cast<double(*)()>(a->address), pure);
					break;
				case TE_FUNCTION1:
					_compiledMu->DefineFun(n, reinterpret_cast<double(*)(double)>(a->address), pure);
					break;
				case TE_FUNCTION2:
					_compiledMu->DefineFun(n, reinterpret_cast<double(*)(double, double)>(a->address), pure);
					break;
				case TE_FUNCTION3:
					_compiledMu->DefineFun(n, reinterpret_cast<double(*)(double, double, double)>(a->address), pure);
					break;
				case TE_FUNCTION4:
					_compiledMu->DefineFun(n, reinterpret_cast<double(*)(double, double, double, double)>(a->address), pure);
					break;
				case TE_FUNCTION5:
					_compiledMu->DefineFun(n, reinterpret_cast<double(*)(double, double, double, double, double)>(a->address), pure);
					break;
				case TE_FUNCTION6:
					_compiledMu->DefineFun(n, reinterpret_cast<double(*)(double, double, double, double, double, double)>(a->address), pure);
					break;
				case TE_FUNCTION7:
					_compiledMu->DefineFun(n, reinterpret_cast<double(*)(double, double, double, double, double, double, double)>(a->address), pure);
					break;
				}
			} catch (mu::Parser::exception_type &e) {
#ifdef UNICODE
				mu::string_type n = e.GetMsg();
				std::string cn;
				cn.reserve(n.size());
				os_wcs_to_utf8(n.c_str(), n.size(), &cn[0], n.size());
				cn = cn.c_str();
#else
				std::string cn = e.GetMsg();
#endif
				blog(LOG_WARNING, "%s", cn.c_str());
				blog(LOG_WARNING, "error adding: %s", name.c_str());
			}
		}

		bool success = true;
		auto debugmsg = [=](mu::Parser::exception_type &e) {
#ifdef UNICODE
			mu::string_type n = e.GetMsg();
			std::string cn;
			cn.reserve(n.size());
			os_wcs_to_utf8(n.c_str(), n.size(), &cn[0], n.size());
			cn = cn.c_str();

			mu::string_type wtoken = e.GetToken();
			std::string token;
			token.reserve(wtoken.size());
			os_wcs_to_utf8(wtoken.c_str(), wtoken.size(), &token[0], wtoken.size());
			token = token.c_str();
#else
			std::string cn = e.GetMsg();
			std::string token = e.GetToken();
#endif
			blog(LOG_ERROR, "%s", cn.c_str());
			blog(LOG_WARNING, "%s", name.c_str());
			int pos = e.GetPos();
			if (pos >= 0) {
				std::string er = "Expression Error At [" + std::to_string(pos) + "] in: " + expression + "\n" +
					expression.substr(0, pos) + "[ERROR HERE]" + expression.substr(pos);
				_errString = er;
				blog(LOG_WARNING, er.c_str());
			} else {
				std::string er = "Expression Error:" + expression;
				blog(LOG_WARNING, er.c_str());
			}
			if (!token.empty()) {
				blog(LOG_WARNING, "Problem token: %s", token.c_str());
			}
			_err = e.GetPos();
			blog(LOG_WARNING, "%i", e.GetCode());
		};

		try {
#ifdef UNICODE
			std::string cn = expression;
			mu::string_type n;
			n.reserve(cn.size());
			os_utf8_to_wcs(cn.c_str(), cn.size(), &n[0], cn.size());
			n = n.c_str();
#else
			std::string n = expression;
#endif
			_compiledMu->SetExpr(n);
		} catch (mu::Parser::exception_type &e) {
			debugmsg(e);
			success = false;
		}

		double test = 0.0;
		try {
			test = _compiledMu->Eval();
		} catch (mu::Parser::exception_type &e) {
			debugmsg(e);
			success = false;
		}
		blog(LOG_DEBUG, "First calcuation: %f", test);

		if (success) {
			_errString = "";
			_expr = expression;
		}
		
		_errStrMap[expression] = _errString;
		_errMap[expression] = _err;
		_compiledMuMap[expression] = _compiledMu;
	}
	bool success()
	{
		return _errString.empty();
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
	int trylock()
	{
		if (_mutexCreated)
			return pthread_mutex_trylock(&_mutex);
		return -1;
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
class ShaderSource;
class ShaderData;

class ShaderParameter {
protected:
	EParam     *_param = nullptr;
	std::string _name;
	std::string _description;

	PThreadMutex *_mutex = nullptr;

	gs_shader_param_type _paramType;

	ShaderData     *_shaderData = nullptr;
	obs_property_t *_property = nullptr;
	ShaderSource   *_filter = nullptr;

public:
	ShaderParameter(gs_eparam_t *param, ShaderSource *filter);
	~ShaderParameter();

	void init(gs_shader_param_type paramType);

	std::string getName();
	std::string getDescription();
	EParam     *getParameter();
	gs_shader_param_type getParameterType();

	void lock();
	void unlock();
	void videoTick(ShaderSource *filter, float elapsed_time, float seconds);
	void videoRender(ShaderSource *filter);
	void update(ShaderSource *filter);
	void getProperties(ShaderSource *filter, obs_properties_t *props);
	void onPass(ShaderSource *filter, const char *technique, size_t pass, gs_texture_t *texture);
	void onTechniqueEnd(ShaderSource *filter, const char *technique, gs_texture_t *texture);
};

class ShaderSource {
protected:
	std::string _effectPath;
	std::string _effectString;

	obs_data_t * _settings = nullptr;

	PThreadMutex *_mutex = nullptr;
	bool          _reloadEffect = true;

	TinyExpr expression;

	obs_source_type _source_type;
public:
	obs_source_type getType()
	{
		return _source_type;
	}
	uint64_t startTimestamp;
	uint64_t stopTimestamp;

	float transitionSeconds = 0.0;

	uint32_t totalWidth;
	uint32_t totalHeight;

	gs_effect_t *effect = nullptr;
	gs_texrender_t *filterTexrender = nullptr;

	double _clickCount = 0.0;
	double _mouseUp = 0.0;
	double _mouseType = 0.0;
	double _screenMousePosX = 0.0;
	double _screenMousePosY = 0.0;
	double _screenIndex = 0.0;
	double _screenMouseVisible = 0.0;
	double _mouseX = 0.0;
	double _mouseY = 0.0;
	double _mouseClickX = 0.0;
	double _mouseClickY = 0.0;
	double _mouseClickScreen = 0.0;
	double _mouseLeave = 0.0;
	double _mouseWheelX = 0.0;
	double _mouseWheelY = 0.0;
	double _mouseWheelDeltaX = 0.0;
	double _mouseWheelDeltaY = 0.0;

	std::vector<double> _screenWidth;
	std::vector<double> _screenHeight;

	double _keyModifiers;
	double _keyUp;
	double _nativeKeyModifiers;
	double _key;
	double _transitionDuration;

	double _focus;

	std::vector<ShaderParameter *>                     paramList = {};
	std::unordered_map<std::string, ShaderParameter *> paramMap;
	std::vector<ShaderParameter *>                     evaluationList = {};

	std::string resizeExpressions[4];
	int         resizeLeft = 0;
	int         resizeRight = 0;
	int         resizeTop = 0;
	int         resizeBottom = 0;

	std::string transitionTimeExpression;
	std::string mixAExpression;
	std::string mixBExpression;
	double mixPercent;

	int baseWidth = 0;
	int baseHeight = 0;

	float          elapsedTime = 0;
	float          transitionPercentage = 0;
	in_shader_data elapsedTimeBinding;

	vec2 uvScale;
	vec2 uvOffset;
	vec2 uvPixelInterval;
	vec2 baseSize;

	matrix4 viewProj;
	gs_eparam_t *image = nullptr;
	gs_eparam_t *image_1 = nullptr;

	obs_source_t *context = nullptr;

	obs_data_t                    *getSettings();
	std::string                    getPath();
	void                           setPath(std::string path);
	void                           prepReload();
	bool                           needsReloading();
	std::vector<ShaderParameter *> parameters();
	void                           clearExpression();
	void                           appendVariable(te_variable var);
	void                           appendVariable(std::string &name, double *binding);

	void compileExpression(std::string expr = "");

	template<class DataType> DataType evaluateExpression(DataType default_value = 0);
	bool                              expressionCompiled();
	std::string                       expressionError();

	ShaderSource(obs_data_t *settings, obs_source_t *source);
	~ShaderSource();

	void lock();
	void unlock();

	uint32_t getWidth();
	uint32_t getHeight();

	void updateCache(gs_eparam_t *param);
	void reload();

	static void             *create(obs_data_t *settings, obs_source_t *source);
	static void              destroy(void *data);
	static const char       *getName(void *unused);
	static void              videoTick(void *data, float seconds);
	static void              videoTickSource(void *data, float seconds);
	static void              videoTickTransition(void *data, float seconds);
	static void              videoRender(void *data, gs_effect_t *effect);
	static void              videoRenderSource(void *data, gs_effect_t *effect);
	static void              videoRenderTransition(void *data, gs_effect_t *effect);
	static bool              audioRenderTransition(void *data, uint64_t *ts_out, struct obs_source_audio_mix *audio, uint32_t mixers, size_t channels, size_t sample_rate);
	static void              transitionStart(void *data);
	static void              transitionStop(void *data);
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
