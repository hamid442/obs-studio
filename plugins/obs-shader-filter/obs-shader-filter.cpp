#include "obs-shader-filter.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs_shader_filter", "en-US")
#define blog(level, msg, ...) blog(level, "shader-filter: " msg, ##__VA_ARGS__)

static void sidechain_capture(void *p, obs_source_t *source,
		const struct audio_data *audio_data, bool muted);

static bool shader_filter_reload_effect_clicked(obs_properties_t *props,
	obs_property_t *property, void *data);

static bool shader_filter_file_name_changed(obs_properties_t *props,
	obs_property_t *p, obs_data_t *settings);

bool is_power_of_two(size_t val)
{
	return val != 0 && (val & (val - 1)) == 0;
}

double hlsl_clamp(double in, double min, double max)
{
	if (in < min)
		return min;
	if (in > max)
		return max;
	return in;
}

#define M_PI_D 3.141592653589793238462643383279502884197169399375
double hlsl_degrees(double radians)
{
	return radians * (180.0 / M_PI_D);
}

double hlsl_rad(double degrees)
{
	return degrees * (M_PI_D / 180.0);
}

double audio_mel_from_hz(double hz)
{
	return 2595 * log10(1 + hz / 700.0);
}

double audio_hz_from_mel(double mel)
{
	return 700 * (pow(10, mel / 2595) - 1);
}

const static double flt_max = FLT_MAX;
const static double flt_min = FLT_MIN;
const static double int_min = INT_MIN;
const static double int_max = INT_MAX;
static double sample_rate;
static double output_channels;

/* Additional likely to be used functions for mathmatical expressions */
void prepFunctions(std::vector<te_variable> *vars)
{
	std::vector<te_variable> funcs = {{"clamp", hlsl_clamp, TE_FUNCTION3},
			{"float_max", &flt_max}, {"float_min", &flt_min},
			{"int_max", &int_max}, {"int_min", &int_min},
			{"sample_rate", &sample_rate},
			{"channels", &output_channels},
			{"mel_from_hz", audio_mel_from_hz, TE_FUNCTION1},
			{"hz_from_mel", audio_hz_from_mel, TE_FUNCTION1},
			{"degrees", hlsl_degrees, TE_FUNCTION1},
			{"radians", hlsl_rad, TE_FUNCTION1}};
	vars->reserve(vars->size() + funcs.size());
	vars->insert(vars->end(), funcs.begin(), funcs.end());
}

size_t search(std::vector<std::string> array, std::string str)
{
	size_t i;
	for (i = 0; i < array.size(); i++) {
		if (array[i] == str)
			return i;
	}
	return -1;
}

std::string toSnakeCase(std::string str)
{
	size_t i;
	char c;
	for (i = 0; i < str.size(); i++) {
		c = str[i];
		if (isupper(c)) {
			str.insert(i++, "_");
			str.assign(i, tolower(c));
		}
	}
	return str;
}

std::string toCamelCase(std::string str)
{
	size_t i;
	char c;
	for (i = 0; i < str.size(); i++) {
		c = str[i];
		if (c == '_') {
			str.erase(i);
			if (i < str.size())
				str.assign(i, toupper(c));
		}
	}
	return str;
}

int getDataSize(enum gs_shader_param_type type)
{
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
	case GS_SHADER_PARAM_BOOL:
		return 1;
	case GS_SHADER_PARAM_MATRIX4X4:
		return 16;
	}
	return 0;
}

bool isFloatType(enum gs_shader_param_type type)
{
	switch (type) {
	case GS_SHADER_PARAM_VEC4:
	case GS_SHADER_PARAM_VEC3:
	case GS_SHADER_PARAM_VEC2:
	case GS_SHADER_PARAM_FLOAT:
	case GS_SHADER_PARAM_MATRIX4X4:
		return true;
	}
	return false;
}

bool isIntType(enum gs_shader_param_type type)
{
	switch (type) {
	case GS_SHADER_PARAM_INT:
	case GS_SHADER_PARAM_INT2:
	case GS_SHADER_PARAM_INT3:
	case GS_SHADER_PARAM_INT4:
		return true;
	}
	return false;
}

class EVal {
public:
	void *data = nullptr;
	size_t size = 0;
	gs_shader_param_type type = GS_SHADER_PARAM_UNKNOWN;
	~EVal()
	{
		if (data)
			bfree(data);
	};

	operator std::vector<float>()
	{
		std::vector<float> d_float;
		std::vector<int> d_int;
		std::vector<bool> d_bool;
		float *ptr_float = static_cast<float*>(data);
		int *ptr_int = static_cast<int*>(data);
		bool *ptr_bool = static_cast<bool*>(data);

		size_t i;
		size_t len;

		switch (type) {
		case GS_SHADER_PARAM_BOOL:
			len = size / sizeof(bool);
			d_float.reserve(len);
			d_bool.assign(ptr_bool, ptr_bool + len);
			for (i = 0; i < d_bool.size(); i++)
				d_float.push_back(d_bool[i]);
			break;
		case GS_SHADER_PARAM_FLOAT:
		case GS_SHADER_PARAM_VEC2:
		case GS_SHADER_PARAM_VEC3:
		case GS_SHADER_PARAM_VEC4:
		case GS_SHADER_PARAM_MATRIX4X4:
			len = size / sizeof(float);
			d_float.assign(ptr_float, ptr_float + len);
			break;
		case GS_SHADER_PARAM_INT:
		case GS_SHADER_PARAM_INT2:
		case GS_SHADER_PARAM_INT3:
		case GS_SHADER_PARAM_INT4:
			len = size / sizeof(int);
			d_float.reserve(len);
			d_int.assign(ptr_int, ptr_int + len);
			for (i = 0; i < d_int.size(); i++)
				d_float.push_back(d_int[i]);
			break;
		}
		return d_float;
	}

	operator std::vector<int>()
	{
		std::vector<float> d_float;
		std::vector<int> d_int;
		std::vector<bool> d_bool;
		float *ptr_float = static_cast<float*>(data);
		int *ptr_int = static_cast<int*>(data);
		bool *ptr_bool = static_cast<bool*>(data);

		size_t i;
		size_t len;

		switch (type) {
		case GS_SHADER_PARAM_BOOL:
			len = size / sizeof(bool);
			d_int.reserve(len);
			d_bool.assign(ptr_bool, ptr_bool + len);
			for (i = 0; i < d_bool.size(); i++)
				d_int.push_back(d_bool[i]);
			break;
		case GS_SHADER_PARAM_FLOAT:
		case GS_SHADER_PARAM_VEC2:
		case GS_SHADER_PARAM_VEC3:
		case GS_SHADER_PARAM_VEC4:
		case GS_SHADER_PARAM_MATRIX4X4:
			len = size / sizeof(float);
			d_int.reserve(len);
			d_float.assign(ptr_float, ptr_float + len);
			for (i = 0; i < d_float.size(); i++)
				d_int.push_back(d_float[i]);
			break;
		case GS_SHADER_PARAM_INT:
		case GS_SHADER_PARAM_INT2:
		case GS_SHADER_PARAM_INT3:
		case GS_SHADER_PARAM_INT4:
			len = size / sizeof(int);
			d_int.assign(ptr_int, ptr_int + len);
			break;
		}
		return d_int;
	}

	operator std::vector<bool>()
	{
		std::vector<float> d_float;
		std::vector<int> d_int;
		std::vector<bool> d_bool;
		float *ptr_float = static_cast<float*>(data);
		int *ptr_int = static_cast<int*>(data);
		bool *ptr_bool = static_cast<bool*>(data);

		size_t i;
		size_t len;

		switch (type) {
		case GS_SHADER_PARAM_BOOL:
			len = size / sizeof(bool);
			d_bool.assign(ptr_bool, ptr_bool + len);
			break;
		case GS_SHADER_PARAM_FLOAT:
		case GS_SHADER_PARAM_VEC2:
		case GS_SHADER_PARAM_VEC3:
		case GS_SHADER_PARAM_VEC4:
		case GS_SHADER_PARAM_MATRIX4X4:
			len = size / sizeof(float);
			d_float.assign(ptr_float, ptr_float + len);
			d_bool.reserve(len);
			for (i = 0; i < d_float.size(); i++)
				d_bool.push_back(d_float[i]);
			break;
		case GS_SHADER_PARAM_INT:
		case GS_SHADER_PARAM_INT2:
		case GS_SHADER_PARAM_INT3:
		case GS_SHADER_PARAM_INT4:
			len = size / sizeof(int);
			d_int.assign(ptr_int, ptr_int + len);
			d_bool.reserve(len);
			for (i = 0; i < d_int.size(); i++)
				d_bool.push_back(d_int[i]);
			break;
		}
		return d_bool;
	}

	operator std::string()
	{
		std::string str = "";
		char *ptr_char = static_cast<char*>(data);

		switch (type) {
		case GS_SHADER_PARAM_STRING:
			str = ptr_char;
			break;
		}
		return str;
	}

	std::string getString()
	{
		return *this;
	}

	const char *c_str()
	{
		return ((std::string)*this).c_str();
	}
};

class EParam {
protected:
	gs_eparam_t *_param;
	gs_effect_param_info _param_info = { 0 };
	std::vector<EParam *> _annotations;
	std::vector<gs_effect_param_info> _annotations_info;
public:
	gs_effect_param_info info() const
	{
		return _param_info;
	}

	int nameCompare(std::string name) const
	{
		return strcmp(this->info().name, name.c_str());
	}

	static EVal *getValue(gs_eparam_t *eparam)
	{
		EVal *v = nullptr;

		if (eparam) {
			gs_effect_param_info note_info;
			gs_effect_get_param_info(eparam, &note_info);

			EVal *v = new EVal();
			v->data = gs_effect_get_default_val(eparam);
			v->size = gs_effect_get_default_val_size(eparam);
			v->type = note_info.type;
		}

		return v;
	}

	gs_eparam_t *getParam()
	{
		return _param;
	}

	EParam *getAnnotation(size_t idx)
	{
		return _annotations.at(idx);
	}
	
	/* Binary Search */
	size_t getAnnotationIndex(std::string name)
	{
		if (_annotations.size() == 0)
			return -1;
		size_t low = 0;
		size_t hi = _annotations.size() - 1;
		int i;
		int r;
		while (low <= hi) {
			i = (low + ((hi - low) / 2));
			r = getAnnotation(i)->nameCompare(name);
			if (r == 0)
				return i;
			else if (r > 0)
				low = i + 1;
			else
				hi = i - 1;
			if (hi == -1)
				break;
		}
		return low;
	}

	EVal *getAnnotationValue(std::string name)
	{
		try {
			size_t i = getAnnotationIndex(name);
			EParam *p = getAnnotation(i);
			gs_eparam_t *par = p->getParam();
			return getValue(par);
		} catch (std::out_of_range err) {
			return nullptr;
		}
	}

	EParam(gs_eparam_t *param)
	{
		_param = param;
		gs_effect_get_param_info(param, &_param_info);

		size_t i;
		size_t num = gs_param_get_num_annotations(_param);
		_annotations.reserve(num);
		_annotations_info.reserve(num);

		gs_eparam_t *p = nullptr;
		std::vector<EParam *>::iterator annotation_it;
		std::vector<gs_effect_param_info>::iterator info_it;

		for (i = 0; i < num; i++) {
			p = gs_param_get_annotation_by_idx(_param, i);
			EParam *ep = new EParam(p);
			gs_effect_param_info _info;
			gs_effect_get_param_info(p, &_info);
			/* Alphabetically order annotations */
			size_t insert = getAnnotationIndex(_info.name);
			if (insert < _annotations.size()) {
				 annotation_it = _annotations.begin() + insert;
				 info_it = _annotations_info.begin() + insert;
				_annotations.insert(annotation_it, ep);
				_annotations_info.insert(info_it, _info);
			} else {
				_annotations.push_back(ep);
				_annotations_info.push_back(_info);
			}
		}
	}

	~EParam()
	{
		while (!_annotations.empty()) {
			EParam *p = _annotations.back();
			_annotations.pop_back();
			delete p;
		}
		_annotations_info.clear();
	}

	template <class DataType>
	void setValue(DataType *data, size_t size)
	{
		size_t len = size / sizeof(DataType);
		size_t arraySize = len * sizeof(DataType);
		gs_effect_set_val(_param, data, arraySize);
	}
};

class ShaderData {
protected:
	ShaderParameterType _type;
	gs_shader_param_type _paramType;

	ShaderFilter *_filter;
	ShaderParameter *_parent;

	std::vector<out_shader_data> _values;
	std::vector<in_shader_data> _bindings;

	std::vector<std::string> _names;
	std::vector<std::string> _descs;
	std::vector<std::string> _binding_names;
	std::vector<std::string> _expressions;

	size_t _dataCount;
public:
	ShaderParameterType getType() const
	{
		return _type;
	}

	gs_shader_param_type getParamType() const
	{
		return _paramType;
	}

	ShaderParameter *getParent()
	{
		return _parent;
	}

	ShaderData(ShaderParameter *parent = nullptr,
			ShaderFilter *filter = nullptr) :
			_parent(parent),
			_filter(filter)
	{
	}

	~ShaderData()
	{
	}

	virtual void init(ShaderParameterType type,
			gs_shader_param_type paramType)
	{
		_type = type;
		_paramType = paramType;

		_dataCount = getDataSize(paramType);

		_names.reserve(_dataCount);
		_descs.reserve(_dataCount);
		_values.reserve(_dataCount);
		_bindings.reserve(_dataCount);
		_expressions.reserve(_dataCount);
		_binding_names.reserve(_dataCount);

		size_t i;
		out_shader_data empty = { 0 };
		in_shader_data emptyBinding = { 0 };
		EParam *e = _parent->getParameter();

		std::string n = _parent->getName();
		std::string d = _parent->getDescription();
		std::string strNum = "";
		EVal *val = nullptr;
		for (i = 0; i < _dataCount; i++) {
			if(_dataCount > 1)
				strNum = "_" + std::to_string(i);
			_names.push_back(n + strNum);
			_descs.push_back(d + strNum);
			_binding_names.push_back(toSnakeCase(_names[i]));
			_values.push_back(empty);
			_bindings.push_back(emptyBinding);

			val = e->getAnnotationValue(_binding_names[i] + "_expr");
			if (val) {
				_expressions.push_back(*val);
				delete val;
			} else {
				_expressions.push_back("");
			}

			te_variable var = { 0 };
			var.address = &_bindings[i];
			var.name = _binding_names[i].c_str();
			if (_filter)
				_filter->appendVariable(var);
		}
	};

	virtual void getProperties(ShaderFilter *filter,
			obs_properties_t *props)
	{
	};

	virtual void videoTick(ShaderFilter *filter, float elapsedTime,
			float seconds)
	{
	};

	virtual void videoRender(ShaderFilter *filter)
	{
	};

	virtual void update(ShaderFilter *filter)
	{
	};
};

class NumericalData : public ShaderData {
	bool _isFloat;
	bool _isInt;
	bool _skipProperty;
	bool _skipCalculations;
public:
	NumericalData(ShaderParameter *parent, ShaderFilter *filter) :
		ShaderData(parent, filter)
	{
	};

	~NumericalData()
	{
	};

	void init(ShaderParameterType type, gs_shader_param_type paramType)
	{
		ShaderData::init(type, paramType);
		_isFloat = isFloatType(paramType);
		_isInt = isIntType(paramType);
		_skipProperty = false;
		_skipCalculations = false;
		size_t i;
		for (i = 0; i < _expressions.size(); i++) {
			if (!_expressions[i].empty()) {
				_skipProperty = true;
				break;
			}
		}
		std::string name = _parent->getName();
		if (name == "ViewProj" || name == "uv_offset"
			|| name == "uv_scale"
			|| name == "uv_pixel_interval"
			|| name == "elapsed_time") {
			_skipProperty = true;
			_skipCalculations = true;
		}
	}

	void getProperties(ShaderFilter *filter, obs_properties_t *props)
	{
		size_t i;
		if (_skipProperty)
			return;
		if (_isFloat) {
			float min = -FLT_MAX;
			float max = FLT_MAX;
			float step = 1.0;
			if (_type == unspecified && _dataCount == 4) {
				obs_properties_add_color(props,
						_names[0].c_str(),
						_descs[0].c_str());
				return;
			}
			for (i = 0; i < _dataCount; i++) {
				switch (_type) {
				default:
					obs_properties_add_float(props,
						_names[i].c_str(),
						_descs[i].c_str(), min, max,
						step);
					break;
				}
			}
		} else if(_isInt) {
			int min = INT_MIN;
			int max = INT_MAX;
			int step = 1;
			for (i = 0; i < _dataCount; i++) {
				switch (_type) {
				default:
					obs_properties_add_int(props,
							_names[i].c_str(),
							_descs[i].c_str(), min,
							max, step);
					break;
				}
			}
		} else {
			for (i = 0; i < _dataCount; i++) {
				switch (_type) {
				default:
					obs_properties_add_bool(props,
							_names[i].c_str(),
							_descs[i].c_str());
				}
			}
		}
	}

	void update(ShaderFilter *filter)
	{
		if (_skipProperty)
			return;
		obs_data_t *settings = filter->getSettings();
		size_t i;
		for (i = 0; i < _dataCount; i++) {
			switch (_paramType) {
			case GS_SHADER_PARAM_BOOL:
				_bindings[i].s64i = obs_data_get_bool(settings,
						_names[i].c_str());
				_values[i].s32i = _bindings[i].s64i;
				break;
			case GS_SHADER_PARAM_INT:
			case GS_SHADER_PARAM_INT2:
			case GS_SHADER_PARAM_INT3:
			case GS_SHADER_PARAM_INT4:
				_bindings[i].s64i = obs_data_get_int(settings,
						_names[i].c_str());
				_values[i].s32i = _bindings[i].s64i;
				break;
			case GS_SHADER_PARAM_FLOAT:
			case GS_SHADER_PARAM_VEC2:
			case GS_SHADER_PARAM_VEC3:
			case GS_SHADER_PARAM_VEC4:
			case GS_SHADER_PARAM_MATRIX4X4:
				_bindings[i].d = obs_data_get_double(settings,
						_names[i].c_str());
				_values[i].f = _bindings[i].d;
				break;
			default:
				break;
			}
		}
	}

	void videoTick(ShaderFilter *filter, float elapsedTime, float seconds)
	{
		size_t i;
		if (_skipCalculations)
			return;
		for (i = 0; i < _dataCount; i++) {
			if (_expressions[i].empty())
				continue;
			switch (_paramType) {
			case GS_SHADER_PARAM_BOOL:
				_filter->compileExpression(_expressions[i]);
				_bindings[i].s64i = filter->evaluateExpression<long long>(0);
				_values[i].s32i = _bindings[i].s64i;
				break;
			case GS_SHADER_PARAM_INT:
			case GS_SHADER_PARAM_INT2:
			case GS_SHADER_PARAM_INT3:
			case GS_SHADER_PARAM_INT4:
				_filter->compileExpression(_expressions[i]);
				_bindings[i].s64i = filter->evaluateExpression<long long>(0);
				_values[i].s32i = _bindings[i].s64i;
				break;
			case GS_SHADER_PARAM_FLOAT:
			case GS_SHADER_PARAM_VEC2:
			case GS_SHADER_PARAM_VEC3:
			case GS_SHADER_PARAM_VEC4:
			case GS_SHADER_PARAM_MATRIX4X4:
				_filter->compileExpression(_expressions[i]);
				_bindings[i].d = filter->evaluateExpression<double>(0);
				_values[i].f = _bindings[i].d;
				break;
			default:
				break;
			}
		}
	}

	void setData()
	{
		EParam *e = _parent->getParameter();
		if (e) {
			if (_isFloat) {
				float *data = (float*)_values.data();
				e->setValue<float>(data, _values.size() * sizeof(float));
			} else {
				int *data = (int*)_values.data();
				e->setValue<int>(data, _values.size() * sizeof(int));
			}
		}
	}

	template <class DataType>
	void setData(DataType t)
	{
		EParam *e = _parent->getParameter();
		if (e)
			e->setValue<DataType>(&t, sizeof(t));
	}

	template <class DataType>
	void setData(std::vector<DataType> t)
	{
		EParam *e = _parent->getParameter();
		if (e)
			e->setValue<DataType>(t.data(), t.size() * sizeof(DataType));
	}

	void videoRender(ShaderFilter *filter)
	{
		if (_skipCalculations)
			return;
		setData();
	}
};

class StringData : public ShaderData {
	std::string _value;

	std::vector<std::string> _binding;
	std::vector<double> _bindings;
public:
	StringData(ShaderParameter *parent, ShaderFilter *filter) :
			ShaderData(parent, filter)
	{
	};

	~StringData()
	{
	};

	void init(ShaderParameterType type, gs_shader_param_type paramType)
	{
		ShaderData::init(type, paramType);
	}
};

class TextureData : public ShaderData {
protected:
	gs_texrender_t *_texrender = nullptr;
	gs_texture_t *_tex =  nullptr;
	uint8_t *_data = nullptr;
	size_t _size;
public:
	TextureData(ShaderParameter *parent, ShaderFilter *filter) :
			ShaderData(parent, filter)
	{
	};

	~TextureData()
	{
		obs_enter_graphics();
		if(_tex)
			gs_texture_destroy(_tex);
		obs_leave_graphics();
		if (_data)
			bfree(_data);
	};

	void init(ShaderParameterType type, gs_shader_param_type paramType)
	{
		_type = type;
		_paramType = paramType;
	}

	void videoRender(ShaderFilter *filter)
	{
		uint32_t src_cx = obs_source_get_width(filter->context);
		uint32_t src_cy = obs_source_get_height(filter->context);
		if (src_cx > 0 && src_cy > 0) {
			if (!_data) {
				_data = static_cast<uint8_t*>(bzalloc(src_cx * src_cy * 16));
			}
			if (!_tex) {
				obs_enter_graphics();
				_tex = gs_texture_create(src_cx, src_cy, GS_RGBA, 1, (const uint8_t**)(&_data), 0);
				obs_leave_graphics();
			}
			if (_tex) {
				EParam *e = _parent->getParameter();
				e->setValue<gs_texture_t*>(&_tex, sizeof(gs_texture_t*));
			}
		}
	}
};

class NullData : public ShaderData {
public:
	NullData(ShaderParameter *parent, ShaderFilter *filter) :
			ShaderData(parent, filter)
	{
	};

	~NullData()
	{
	};
};


std::string ShaderParameter::getName()
{
	return _name;
}

std::string ShaderParameter::getDescription()
{
	return _description;
}

EParam *ShaderParameter::getParameter()
{
	return _param;
}

ShaderParameter::ShaderParameter(gs_eparam_t *param, ShaderFilter *filter) :
	_filter(filter)
{
	struct gs_effect_param_info info;
	gs_effect_get_param_info(param, &info);

	pthread_mutexattr_t attr;
	if (pthread_mutexattr_init(&attr) != 0) {
		_mutex_created = false;
		return;
	}
	if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0) {
		_mutex_created = false;
		return;
	}

	_mutex_created = pthread_mutex_init(&_mutex, NULL) == 0;
	_name = info.name;
	_description = info.name;
	_param = new EParam(param);

	ShaderParameterType t = unspecified;

	EVal *v = _param->getAnnotationValue("type");
	std::vector<std::string> map =
	{
		"vec4",
		"list",
		"media",
		"source",
		"audio_waveform",
		"audio_fft",
		"audio_power_spectrum"
	};

	if (v) {
		size_t index = search(map, *v);
		t = (ShaderParameterType)(index + 1);
	}

	init(t, info.type);
	if(v)
		delete v;
}

void ShaderParameter::init(ShaderParameterType type,
		gs_shader_param_type paramType)
{
	lock();
	_type = type;
	_paramType = paramType;
	switch (paramType) {
	case GS_SHADER_PARAM_BOOL:
	case GS_SHADER_PARAM_INT:
	case GS_SHADER_PARAM_INT2:
	case GS_SHADER_PARAM_INT3:
	case GS_SHADER_PARAM_INT4:
	case GS_SHADER_PARAM_FLOAT:
	case GS_SHADER_PARAM_VEC2:
	case GS_SHADER_PARAM_VEC3:
	case GS_SHADER_PARAM_VEC4:
	case GS_SHADER_PARAM_MATRIX4X4:
		_shaderData = new NumericalData(this, _filter);
		break;
	case GS_SHADER_PARAM_TEXTURE:
		_shaderData = new TextureData(this, _filter);
		break;
	case GS_SHADER_PARAM_STRING:
		_shaderData = new StringData(this, _filter);
		break;
	case GS_SHADER_PARAM_UNKNOWN:
		_shaderData = new NullData(this, _filter);
		break;
	}
	if (_shaderData)
		_shaderData->init(type, paramType);
	unlock();
}

ShaderParameter::~ShaderParameter()
{
	if (_mutex_created)
		pthread_mutex_destroy(&_mutex);

	if (_param)
		delete _param;
}

void ShaderParameter::lock()
{
	if (_mutex_created)
		pthread_mutex_lock(&_mutex);
}

void ShaderParameter::unlock()
{
	if (_mutex_created)
		pthread_mutex_unlock(&_mutex);
}

void ShaderParameter::videoTick(ShaderFilter *filter, float elapsed_time, float seconds)
{
	lock();
	if (_shaderData)
		_shaderData->videoTick(filter, elapsed_time, seconds);
	unlock();
}

void ShaderParameter::videoRender(ShaderFilter *filter)
{
	lock();
	if (_shaderData)
		_shaderData->videoRender(filter);
	unlock();
}

void ShaderParameter::update(ShaderFilter *filter)
{
	lock();
	if (_shaderData)
		_shaderData->update(filter);
	unlock();
}

void ShaderParameter::getProperties(ShaderFilter *filter, obs_properties_t *props)
{
	lock();
	if (_shaderData)
		_shaderData->getProperties(filter, props);
	unlock();
}

obs_data_t *ShaderFilter::getSettings()
{
	return _settings;
}

std::string ShaderFilter::getPath()
{
	return _effect_path;
}

void ShaderFilter::setPath(std::string path)
{
	_effect_path = path;
}

void ShaderFilter::prepReload()
{
	_reload_effect = true;
}

bool ShaderFilter::needsReloading()
{
	return _reload_effect;
}

std::vector<ShaderParameter*> ShaderFilter::parameters()
{
	return paramList;
}

void ShaderFilter::clearExpression()
{
	expression.clear();
}

void ShaderFilter::appendVariable(te_variable var)
{
	expression.push_back(var);
}

void ShaderFilter::compileExpression(std::string expresion)
{
	expression.compile(expresion);
}

template <class DataType>
DataType ShaderFilter::evaluateExpression(DataType default_value)
{
	return expression.evaluate(default_value);
}

ShaderFilter::ShaderFilter(obs_data_t *settings, obs_source_t *source)
{
	context = source;
	_settings = settings;
	pthread_mutexattr_t attr;
	if (pthread_mutexattr_init(&attr) != 0) {
		_mutex_created = false;
		return;
	}
	if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0) {
		_mutex_created = false;
		return;
	}
	_mutex_created = pthread_mutex_init(&_mutex, NULL) == 0;
	update(this, _settings);
};

ShaderFilter::~ShaderFilter()
{
};

void ShaderFilter::lock()
{
	if (_mutex_created)
		pthread_mutex_lock(&_mutex);
}

void ShaderFilter::unlock()
{
	if (_mutex_created)
		pthread_mutex_unlock(&_mutex);
}

uint32_t ShaderFilter::getWidth()
{
	return total_width;
}
uint32_t ShaderFilter::getHeight()
{
	return total_height;
}

void ShaderFilter::updateCache(gs_eparam_t *param)
{
	struct gs_effect_param_info info;
	gs_effect_get_param_info(param, &info);

	std::string n = info.name;
	ShaderParameter *p = new ShaderParameter(param, this);
	if (n == "ViewProj") {
		//matrix4_pairs.push_back(std::pair<gs_eparam_t*, matrix4*>(param, &view_proj));
	} else if (n == "uv_offset") {
		vec2_pairs.push_back(std::pair<gs_eparam_t*, vec2*>(param, &uv_offset));
	} else if (n == "uv_scale") {
		vec2_pairs.push_back(std::pair<gs_eparam_t*, vec2*>(param, &uv_scale));
	} else if (n == "uv_pixel_interval") {
		vec2_pairs.push_back(std::pair<gs_eparam_t*, vec2*>(param, &uv_pixel_interval));
	} else if (n == "elapsed_time") {
		/*
		if (elapsedTime)
			delete elapsedTime;
		elapsedTime = p;
		*/
	} else {
		paramList.push_back(p);
	}
}

void ShaderFilter::reload()
{
	size_t i;
	char *errors = NULL;

	/* Clear previous settings */
	while (!paramList.empty()) {
		ShaderParameter *p = paramList.back();
		paramList.pop_back();
		delete p;
	}
	evaluationList.clear();
	expression.clear();

	prepFunctions(&expression);

	obs_enter_graphics();
	gs_effect_destroy(effect);
	effect = nullptr;
	obs_leave_graphics();

	_effect_path = obs_data_get_string(_settings,
		"shader_file_name");
	/* Load default effect text if no file is selected */
	if (!_effect_path.empty())
		_effect_string = os_quick_read_utf8_file(
			_effect_path.c_str());
	else
		return;

	obs_enter_graphics();
	effect = gs_effect_create(_effect_string.c_str(), NULL,
		&errors);
	obs_leave_graphics();

	size_t effect_count = gs_effect_get_num_params(effect);
	for (i = 0; i < effect_count; i++) {
		gs_eparam_t *param = gs_effect_get_param_by_idx(effect, i);
		updateCache(param);
	}
}

void *ShaderFilter::create(obs_data_t *settings, obs_source_t *source)
{
	ShaderFilter *filter = new ShaderFilter(settings, source);
	return filter;
}

void ShaderFilter::destroy(void *data)
{
	delete static_cast<ShaderFilter*>(data);
}

const char *ShaderFilter::getName(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("ShaderFilter");
}

void ShaderFilter::videoTick(void *data, float seconds)
{
	ShaderFilter *filter = static_cast<ShaderFilter*>(data);
	filter->elapsed_time += seconds;

	size_t i;
	std::vector<ShaderParameter*> parameters = filter->parameters();
	for (i = 0; i < parameters.size(); i++) {
		if (parameters[i]) {
			parameters[i]->videoTick(filter, filter->elapsed_time, seconds);
		}
	}

	obs_source_t *target = obs_filter_get_target(filter->context);
	/* Determine offsets from expansion values. */
	int base_width = obs_source_get_base_width(target);
	int base_height = obs_source_get_base_height(target);

	filter->total_width = filter->resize_left + base_width +
		filter->resize_right;
	filter->total_height = filter->resize_top + base_height +
		filter->resize_bottom;

	filter->uv_scale.x = (float)filter->total_width / base_width;
	filter->uv_scale.y = (float)filter->total_height / base_height;

	filter->uv_offset.x = (float)(-filter->resize_left) / base_width;
	filter->uv_offset.y = (float)(-filter->resize_top) / base_height;

	filter->uv_pixel_interval.x = 1.0f / base_width;
	filter->uv_pixel_interval.y = 1.0f / base_height;
}

void ShaderFilter::videoRender(void *data, gs_effect_t *effect)
{
	ShaderFilter *filter = static_cast<ShaderFilter*>(data);
	size_t i;

	if (filter->effect != nullptr) {
		if (!obs_source_process_filter_begin(filter->context,
			GS_RGBA, OBS_NO_DIRECT_RENDERING))
			return;

		std::vector<ShaderParameter*> parameters =
				filter->parameters();

		for (i = 0; i < filter->vec2_pairs.size(); i++) {
			gs_effect_set_vec2(filter->vec2_pairs[i].first, filter->vec2_pairs[i].second);
		}

		for (i = 0; i < parameters.size(); i++) {
			if (parameters[i]) {
				parameters[i]->videoRender(filter);
			}
		}

		obs_source_process_filter_end(filter->context,
			filter->effect, filter->total_width,
			filter->total_height);
	} else {
		obs_source_skip_video_filter(filter->context);
	}
}

void ShaderFilter::update(void *data, obs_data_t *settings)
{
	ShaderFilter *filter = static_cast<ShaderFilter*>(data);
	if (filter->needsReloading())
		filter->reload();
	size_t i;
	std::vector<ShaderParameter*> parameters = filter->parameters();
	for (i = 0; i < parameters.size(); i++) {
		if (parameters[i]) {
			parameters[i]->update(filter);
		}
	}
}

obs_properties_t *ShaderFilter::getProperties(void *data)
{
	ShaderFilter *filter = static_cast<ShaderFilter*>(data);
	size_t i;
	obs_properties_t *props = obs_properties_create();
	obs_properties_set_param(props, filter, NULL);

	std::string defaultPath = obs_get_module_data_path(
		obs_current_module());
	defaultPath += "/shaders";

	obs_properties_add_button(props, "reload_effect",
		_MT("ShaderFilter.ReloadEffect"),
		shader_filter_reload_effect_clicked);

	obs_property_t *file_name = obs_properties_add_path(props,
		"shader_file_name",
		_MT("ShaderFilter.ShaderFileName"),
		OBS_PATH_FILE, NULL,
		defaultPath.c_str());

	obs_property_set_modified_callback(file_name,
		shader_filter_file_name_changed);

	std::vector<ShaderParameter*> parameters = filter->parameters();
	for (i = 0; i < parameters.size(); i++) {
		if (parameters[i]) {
			parameters[i]->getProperties(filter, props);
		}
	}
	return props;
}

uint32_t ShaderFilter::getWidth(void *data)
{
	ShaderFilter *filter = static_cast<ShaderFilter*>(data);
	return filter->getWidth();
}

uint32_t ShaderFilter::getHeight(void *data)
{
	ShaderFilter *filter = static_cast<ShaderFilter*>(data);
	return filter->getHeight();
}

void ShaderFilter::getDefaults(obs_data_t *settings)
{
}

static bool shader_filter_reload_effect_clicked(obs_properties_t *props,
	obs_property_t *property, void *data)
{
	ShaderFilter *filter = static_cast<ShaderFilter*>(data);
	filter->prepReload();
	obs_source_update(filter->context, NULL);

	return true;
}

static bool shader_filter_file_name_changed(obs_properties_t *props,
	obs_property_t *p, obs_data_t *settings)
{
	ShaderFilter *filter = static_cast<ShaderFilter*>(
		obs_properties_get_param(props));
	std::string path = obs_data_get_string(settings, obs_property_name(p));

	if (filter->getPath() != path) {
		filter->prepReload();
		filter->setPath(path);
	}

	return true;
}

bool obs_module_load(void)
{
	struct obs_source_info shader_filter = { 0 };
	shader_filter.id = "obs_shader_filter";
	shader_filter.type = OBS_SOURCE_TYPE_FILTER;
	shader_filter.output_flags = OBS_SOURCE_VIDEO;
	shader_filter.get_name = ShaderFilter::getName;
	shader_filter.create = ShaderFilter::create;
	shader_filter.destroy = ShaderFilter::destroy;
	shader_filter.update = ShaderFilter::update;
	shader_filter.video_tick = ShaderFilter::videoTick;
	shader_filter.video_render = ShaderFilter::videoRender;
	shader_filter.get_defaults = ShaderFilter::getDefaults;
	shader_filter.get_width = ShaderFilter::getWidth;
	shader_filter.get_height = ShaderFilter::getHeight;
	shader_filter.get_properties = ShaderFilter::getProperties;

	obs_register_source(&shader_filter);

	struct obs_audio_info aoi;
	obs_get_audio_info(&aoi);
	sample_rate = (double)aoi.samples_per_sec;
	output_channels = (double)get_audio_channels(aoi.speakers);

	return true;
}

void obs_module_unload(void)
{
}
