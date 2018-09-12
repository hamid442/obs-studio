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

static const char *shader_filter_texture_file_filter =
"Textures (*.bmp *.tga *.png *.jpeg *.jpg *.gif);;";

static const char *shader_filter_media_file_filter =
"Video Files (*.mp4 *.ts *.mov *.wmv *.flv *.mkv *.avi *.gif *.webm);;";

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
	float defaultFloat = 0.0;
	int defaultInt = 0;

	void *data = nullptr;
	size_t size = 0;
	gs_shader_param_type type = GS_SHADER_PARAM_UNKNOWN;
	EVal()
	{

	};
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

	explicit operator float()
	{
		std::vector<float> ret = (std::vector<float>)*this;
		if (ret.size())
			return ret[0];
		else
			return defaultFloat;
	}

	operator int()
	{
		std::vector<int> ret = (std::vector<int>)*this;
		if (ret.size())
			return ret[0];
		else
			defaultInt;
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
private:
	EVal *getValue(gs_eparam_t *eparam)
	{
		EVal *v = nullptr;

		if (eparam) {
			gs_effect_param_info note_info;
			gs_effect_get_param_info(eparam, &note_info);

			v = new EVal();
			v->data = gs_effect_get_default_val(eparam);
			v->size = gs_effect_get_default_val_size(eparam);
			v->type = note_info.type;
		}

		return v;
	}
protected:
	gs_eparam_t *_param = nullptr;
	gs_effect_param_info _param_info = { 0 };
	EVal *_value = nullptr;
	std::unordered_map<std::string, EParam *> _annotations_map;
	size_t _annotationCount;
public:
	gs_effect_param_info info() const
	{
		return _param_info;
	}

	EVal *getValue()
	{
		return _value != nullptr ? _value : (_value = getValue(_param));
	}

	gs_eparam_t *getParam()
	{
		return _param;
	}

	operator gs_eparam_t*()
	{
		return _param;
	}

	size_t getAnnotationCount()
	{
		return _annotations_map.size();
	}

	/* Hash Map Search */
	EParam *getAnnotation(std::string name)
	{
		if (_annotations_map.find(name) != _annotations_map.end())
			return _annotations_map.at(name);
		else
			return nullptr;
	}

	EVal *getAnnotationValue(std::string name)
	{
		if (name == "texture_type" && _annotationCount == 4)
			blog(LOG_INFO, "");

		EParam *note = getAnnotation(name);
		if (note)
			return note->getValue();
		else
			return nullptr;
	}

	bool hasAnnotation(std::string name)
	{
		return _annotations_map.count(name);
	}

	EParam(gs_eparam_t *param)
	{
		_param = param;
		gs_effect_get_param_info(param, &_param_info);
		_value = getValue(param);

		size_t i;
		_annotationCount = gs_param_get_num_annotations(_param);
		_annotations_map.reserve(_annotationCount);

		gs_eparam_t *p = nullptr;
		std::vector<EParam *>::iterator annotation_it;
		std::vector<gs_effect_param_info>::iterator info_it;

		for (i = 0; i < _annotationCount; i++) {
			p = gs_param_get_annotation_by_idx(_param, i);
			EParam *ep = new EParam(p);
			gs_effect_param_info _info;
			gs_effect_get_param_info(p, &_info);

			_annotations_map.insert(std::pair<std::string, EParam *>
					(_info.name, ep));
		}
	}

	~EParam()
	{
		if (_value)
			delete _value;
		for(const auto &annotation : _annotations_map)
			delete annotation.second;
		_annotations_map.erase(_annotations_map.begin(),
				_annotations_map.end());
	}

	template <class DataType>
	void setValue(DataType *data, size_t size)
	{
		size_t len = size / sizeof(DataType);
		size_t arraySize = len * sizeof(DataType);
		gs_effect_set_val(_param, data, arraySize);
	}

	template <class DataType>
	void setValue(std::vector<DataType> data)
	{
		size_t arraySize = data.size() * sizeof(DataType);
		gs_effect_set_val(_param, data.data(), arraySize);
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

	virtual ~ShaderData()
	{
	};

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
			if (val)
				_expressions.push_back(*val);
			else
				_expressions.push_back("");

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
	enum BindType {
		unspecified,
		none,
		byte,
		short_integer,
		integer,
		floating_point,
		double_point
	};
	void *_bind = nullptr;
	BindType bindType;
public:
	NumericalData(ShaderParameter *parent, ShaderFilter *filter) :
		ShaderData(parent, filter)
	{
		gs_eparam_t *param = parent->getParameter()->getParam();
		struct gs_effect_param_info info;
		gs_effect_get_param_info(param, &info);
		/* std::vector<DataType> *bind */
		std::string n = info.name;
		if (n == "ViewProj") {
			bindType = floating_point;
			_bind = &_filter->view_proj;
		} else if (n == "uv_offset") {
			bindType = floating_point;
			_bind = &_filter->uvOffset;
		} else if (n == "uv_scale") {
			bindType = floating_point;
			_bind = &_filter->uvScale;
		} else if (n == "uv_pixel_interval") {
			bindType = floating_point;
			_bind = &_filter->uvPixelInterval;
		} else if (n == "elapsed_time") {
			bindType = floating_point;
			_bind = &_filter->elapsedTime;
		}
	};

	~NumericalData()
	{
	};

	void init(ShaderParameterType type, gs_shader_param_type paramType)
	{
		ShaderData::init(type, paramType);
		_isFloat = isFloatType(paramType);
		_isInt = isIntType(paramType);
		_skipProperty = _bind ? true : false;
		_skipCalculations = false;
		size_t i;
		if (_skipProperty) {
			for (i = 0; i < _expressions.size(); i++) {
				if (!_expressions[i].empty()) {
					_skipProperty = true;
					break;
				}
			}
		}
		//std::string name = _parent->getName();
	}

	void getProperties(ShaderFilter *filter, obs_properties_t *props)
	{
		size_t i;
		if (_bind)
			return;
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
		if (_bind)
			return;
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
			if (!_expressions[i].empty()) {
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
			} else if (_bind) {
				switch (_paramType) {
				case GS_SHADER_PARAM_BOOL:
					_bindings[i].s64i = static_cast<bool*>(_bind)[i];
					_values[i].s32i = _bindings[i].s64i;
					break;
				case GS_SHADER_PARAM_INT:
				case GS_SHADER_PARAM_INT2:
				case GS_SHADER_PARAM_INT3:
				case GS_SHADER_PARAM_INT4:
					_bindings[i].s64i = static_cast<int*>(_bind)[i];
					_values[i].s32i = _bindings[i].s64i;
					break;
				case GS_SHADER_PARAM_FLOAT:
				case GS_SHADER_PARAM_VEC2:
				case GS_SHADER_PARAM_VEC3:
				case GS_SHADER_PARAM_VEC4:
				case GS_SHADER_PARAM_MATRIX4X4:
					_bindings[i].d = static_cast<float*>(_bind)[i];
					_values[i].f = _bindings[i].d;
					break;
				default:
					break;
				}
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

/* functions to add sources to a list for use as textures */
static bool fillPropertiesSourceList(void *param, obs_source_t *source)
{
	obs_property_t *p = (obs_property_t *)param;
	uint32_t flags = obs_source_get_output_flags(source);
	const char *source_name = obs_source_get_name(source);

	if ((flags & OBS_SOURCE_VIDEO) != 0 && obs_source_active(source))
		obs_property_list_add_string(p, source_name, source_name);

	return true;
}

static void fillSourceList(obs_property_t *p)
{
	obs_property_list_add_string(p, _MT("None"), "");
	obs_enum_sources(&fillPropertiesSourceList, (void *)p);
}

static bool fillPropertiesAudioSourceList(void *param, obs_source_t *source)
{
	obs_property_t *p = (obs_property_t *)param;
	uint32_t flags = obs_source_get_output_flags(source);
	const char *source_name = obs_source_get_name(source);

	if ((flags & OBS_SOURCE_AUDIO) != 0 && obs_source_active(source))
		obs_property_list_add_string(p, source_name, source_name);

	return true;
}

static void fillAudioSourceList(obs_property_t *p)
{
	obs_property_list_add_string(p, _MT("None"), "");
	obs_enum_sources(&fillPropertiesAudioSourceList, (void *)p);
}

class TextureData : public ShaderData {
private:
	void renderSource(EParam *param, uint32_t cx, uint32_t cy)
	{
		if (!param)
			return;
		uint32_t media_cx = obs_source_get_width(_mediaSource);
		uint32_t media_cy = obs_source_get_height(_mediaSource);

		if (!media_cx || !media_cy)
			return;

		float scale_x = cx / (float)media_cx;
		float scale_y = cy / (float)media_cy;

		gs_texrender_reset(_texrender);
		if (gs_texrender_begin(_texrender, media_cx, media_cy)) {
			struct vec4 clear_color;
			vec4_zero(&clear_color);

			gs_clear(GS_CLEAR_COLOR, &clear_color, 1, 0);
			gs_matrix_scale3f(scale_x, scale_y, 1.0f);
			obs_source_video_render(_mediaSource);

			gs_texrender_end(_texrender);
		} else {
			return;
		}

		gs_texture_t *tex = gs_texrender_get_texture(_texrender);
		gs_effect_set_texture(*param, tex);
	}

	uint32_t processAudio(size_t samples)
	{
		size_t i;
		size_t j;
		size_t k;
		size_t h_samples = samples / 2;
		size_t h_sample_size = samples * 2;

		for (i = 0; i < _channels; i++) {
			audio_fft_complex(((float*)_data) + (i * samples),
				(uint32_t)samples);
		}
		for (i = 1; i < _channels; i++) {
			memcpy(((float*)_data) + (i * h_samples),
				((float*)_data) + (i * samples),
				h_sample_size);
		}
		/* Calculate a periodogram */
		/*
		if (param->fft_bins < h_samples) {
			size_t bin_width = h_samples / param->fft_bins;
			for (i = 0; i < param->num_channels; i++) {
				for (j = 0; j < param->fft_bins; j++) {
					float bin_sum = 0;
					for (k = 0; k < bin_width; k++) {
						bin_sum += param->sidechain_buf[k +
							i * h_samples +
							j * bin_width];
					}
					param->sidechain_buf[i * param->fft_bins + j] =
						bin_sum / bin_width;
				}
			}
			return param->fft_bins;
		}
		*/
		return (uint32_t)h_samples;
	}

	void renderAudioSource(EParam *param, uint64_t samples)
	{
		if (!_data)
			_data = (uint8_t*)bzalloc(_maxAudioSize * _channels * sizeof(float));
		size_t px_width = samples;
		memcpy(_data, _audio->data(), samples * sizeof(float));
		if (_isFFT)
			processAudio(samples);

		obs_enter_graphics();
		gs_texture_destroy(_tex);
		_tex = gs_texture_create((uint32_t)px_width,
				(uint32_t)_channels, GS_R32F, 1,
				(const uint8_t **)&_data, 0);
		obs_leave_graphics();
		gs_effect_set_texture(*param, _tex);
	}

	void updateAudioSource(std::string name)
	{
		obs_source_t *sidechain = nullptr;
		if (!name.empty())
			sidechain = obs_get_source_by_name(name.c_str());
		obs_source_t *old_sidechain = _mediaSource;

		if (old_sidechain) {
			obs_source_remove_audio_capture_callback(old_sidechain,
					sidechain_capture, this);
			obs_source_release(old_sidechain);
			for (size_t i = 0; i < MAX_AV_PLANES; i++)
				_audio[i].clear();
		}
		if (sidechain)
			obs_source_add_audio_capture_callback(sidechain,
					sidechain_capture, this);
		_mediaSource = sidechain;
	}


protected:
	gs_texrender_t * _texrender = nullptr;
	gs_texture_t *_tex = nullptr;
	gs_image_file_t *_image = nullptr;
	std::vector<float> _audio[MAX_AV_PLANES];
	bool _isFFT = false;
	std::vector<float> _fft_data[MAX_AV_PLANES];
	size_t _channels = 0;
	size_t _maxAudioSize = AUDIO_OUTPUT_FRAMES;
	uint8_t *_data = nullptr;
	obs_source_t *_mediaSource = nullptr;
	std::string _sourceName = "";
	size_t _size;
	enum TextureType {
		ignored,
		unspecified,
		source,
		audio,
		image,
		media
	};
	fft_windowing_type _window;
	TextureType _texType;
public:
	TextureData(ShaderParameter *parent, ShaderFilter *filter) :
			ShaderData(parent, filter),
			_maxAudioSize(AUDIO_OUTPUT_FRAMES)
	{
		_maxAudioSize = AUDIO_OUTPUT_FRAMES;
	};

	~TextureData()
	{
		obs_enter_graphics();
		gs_texrender_destroy(_texrender);
		gs_image_file_free(_image);
		gs_texture_destroy(_tex);
		obs_leave_graphics();
		if (_data)
			bfree(_data);
	};

	size_t getAudioChannels()
	{
		return _channels;
	}

	void insertAudio(float* data, size_t samples, size_t index)
	{
		if (!samples || index > (MAX_AV_PLANES - 1))
			return;
		size_t old_size = _audio[index].size() * sizeof(float);
		size_t insert_size = samples * sizeof(float);
		float* old_data = nullptr;
		if (old_size)
			old_data = (float*)bmemdup(_audio[index].data(), old_size);
		_audio[index].resize(AUDIO_OUTPUT_FRAMES);
		if (samples < AUDIO_OUTPUT_FRAMES) {
			if (old_data)
				memcpy(&_audio[index][samples], old_data, old_size - insert_size);
			if (data)
				memcpy(&_audio[index][0], data, insert_size);
			else
				memset(&_audio[index][0], 0, insert_size);
		} else {
			if (data)
				memcpy(&_audio[index][0], data,
					AUDIO_OUTPUT_FRAMES * sizeof(float));
			else
				memset(&_audio[index][0], 0,
					AUDIO_OUTPUT_FRAMES * sizeof(float));
		}
		bfree(old_data);
	}

	void init(ShaderParameterType type, gs_shader_param_type paramType)
	{
		_type = type;
		_paramType = paramType;
		_names.push_back(_parent->getName());
		_descs.push_back(_parent->getDescription());

		EParam *e = _parent->getParameter();
		EVal *texType = e->getAnnotationValue("texture_type");
		std::unordered_map<std::string, uint32_t> types = {
			{"source", source},
			{"audio", audio},
			{"image", image},
			{"media", media}
		};

		try {
			if (texType) {
				std::string t = texType->getString();
				_texType = (TextureType)types.at(t);
			} else {
				_texType = image;
			}
		} catch (std::out_of_range err) {
			_texType = image;
		}

		if (_names[0] == "image")
			_texType = ignored;

		_channels = audio_output_get_channels(obs_get_audio());
		if (_texType == audio) {
			EVal *channels = e->getAnnotationValue("channels");
			if (channels)
				_channels = *channels;

			for(size_t i = 0; i < MAX_AV_PLANES; i++)
				_audio->resize(AUDIO_OUTPUT_FRAMES);

			EVal *fft = e->getAnnotationValue("is_fft");
			if (fft)
				_isFFT = *fft;
			else
				_isFFT = false;

			EVal *window = e->getAnnotationValue("window");
			if (window)
				_window = get_window_type(window->c_str());
			else
				_window = none;
		}
	}

	void getProperties(ShaderFilter *filter, obs_properties_t *props)
	{;
		obs_property_t *p = nullptr;
		switch (_texType) {
		case source:
			p = obs_properties_add_list(props, _names[0].c_str(),
					_descs[0].c_str(), OBS_COMBO_TYPE_LIST,
					OBS_COMBO_FORMAT_STRING);
			fillSourceList(p);
			break;
		case audio:
			p = obs_properties_add_list(props, _names[0].c_str(),
					_descs[0].c_str(), OBS_COMBO_TYPE_LIST,
					OBS_COMBO_FORMAT_STRING);
			fillAudioSourceList(p);
			break;
		case media:
			p = obs_properties_add_path(props, _names[0].c_str(),
					_descs[0].c_str(), OBS_PATH_FILE,
					shader_filter_media_file_filter,
					NULL);
			break;
		case image:
			p = obs_properties_add_path(props, _names[0].c_str(),
					_descs[0].c_str(), OBS_PATH_FILE,
					shader_filter_texture_file_filter,
					NULL);
			break;
		}
	}

	void update(ShaderFilter *filter)
	{
		obs_data_t *settings = filter->getSettings();
		_channels = audio_output_get_channels(obs_get_audio());
		switch (_texType) {
		case source:
			if (!_texrender)
				_texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
			obs_source_release(_mediaSource);
			_mediaSource = obs_get_source_by_name(
					obs_data_get_string(settings,
					_names[0].c_str()));
			break;
		case audio:
			updateAudioSource(obs_data_get_string(settings,
					_names[0].c_str()));
			break;
		case image:
			if (!_image) {
				_image = (gs_image_file_t *)bzalloc(sizeof(gs_image_file_t));
			} else {
				obs_enter_graphics();
				gs_image_file_free(_image);
				obs_leave_graphics();
			}
			gs_image_file_init(_image, obs_data_get_string(settings,
					_names[0].c_str()));
			obs_enter_graphics();
			gs_image_file_init_texture(_image);
			obs_leave_graphics();
			break;
		}
	}

	void videoRender(ShaderFilter *filter)
	{
		uint32_t src_cx = obs_source_get_width(filter->context);
		uint32_t src_cy = obs_source_get_height(filter->context);
		EParam *e = _parent->getParameter();
		gs_texture_t *t;
		switch (_texType) {
		case media:
		case source:
			renderSource(e, src_cx, src_cy);
			break;
		case audio:
			renderAudioSource(e, AUDIO_OUTPUT_FRAMES);
			break;
		case image:
			if (_image)
				t = _image->texture;
			else
				t = nullptr;
			e->setValue<gs_texture_t*>(&t, sizeof(gs_texture_t*));
			break;
		default:
			break;
		}
	}
};

static void sidechain_capture(void *p, obs_source_t *source,
		const struct audio_data *audio_data, bool muted)
{
	struct TextureData *data = static_cast<TextureData *>(p);

	UNUSED_PARAMETER(source);

	size_t i;
	if (muted) {
		for (i = 0; i < data->getAudioChannels(); i++)
			data->insertAudio(nullptr, audio_data->frames, i);
	} else {
		for (i = 0; i < data->getAudioChannels(); i++)
			data->insertAudio((float*)audio_data->data[i],
					audio_data->frames, i);
	}
}

class NullData : public ShaderData {
public:
	NullData(ShaderParameter *parent, ShaderFilter *filter) :
			ShaderData(parent, filter)
	{
	};

	~NullData()
	{
	};
	void init(ShaderParameterType type,
			gs_shader_param_type paramType)
	{
	}
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
		delete v;
	}

	init(t, info.type);
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

	if (_shaderData)
		delete _shaderData;
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
	prepReload();
	update(this, _settings);
};

ShaderFilter::~ShaderFilter()
{
	/* Clear previous settings */
	while (!paramList.empty()) {
		ShaderParameter *p = paramList.back();
		paramList.pop_back();
		delete p;
	}
	/*
	std::vector<ShaderParameter *>().swap(paramList);
	std::vector<ShaderParameter *>().swap(evaluationList);
	std::vector<te_variable>().swap(expression);
	//paramList.clear();
	//evaluationList.clear();
	//expression.clear();
	*/
	obs_enter_graphics();
	gs_effect_destroy(effect);
	effect = nullptr;
	obs_leave_graphics();
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
	ShaderParameter *p = new ShaderParameter(param, this);
	if(p)
		paramList.push_back(p);
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
	char *effect_string = nullptr;
	if (!_effect_path.empty())
		effect_string = os_quick_read_utf8_file(_effect_path.c_str());
	else
		return;

	obs_enter_graphics();
	effect = gs_effect_create(effect_string, NULL, &errors);
	obs_leave_graphics();

	_effect_string = effect_string;
	bfree(effect_string);

	size_t effect_count = gs_effect_get_num_params(effect);
	paramList.reserve(effect_count);
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
	filter->elapsedTimeBinding.d += seconds;
	filter->elapsedTime += seconds;

	size_t i;
	std::vector<ShaderParameter*> parameters = filter->parameters();
	for (i = 0; i < parameters.size(); i++) {
		if (parameters[i])
			parameters[i]->videoTick(filter, filter->elapsedTime,
					seconds);
	}

	obs_source_t *target = obs_filter_get_target(filter->context);
	/* Determine offsets from expansion values. */
	int baseWidth = obs_source_get_base_width(target);
	int baseHeight = obs_source_get_base_height(target);

	filter->total_width = filter->resizeLeft + baseWidth +
			filter->resizeRight;
	filter->total_height = filter->resizeTop + baseHeight +
			filter->resizeBottom;

	filter->uvScale.x = (float)filter->total_width / baseWidth;
	filter->uvScale.y = (float)filter->total_height / baseHeight;
	filter->uvOffset.x = (float)(-filter->resizeLeft) / baseWidth;
	filter->uvOffset.y = (float)(-filter->resizeTop) / baseHeight;
	filter->uvPixelInterval.x = 1.0f / baseWidth;
	filter->uvPixelInterval.y = 1.0f / baseHeight;

	filter->uvScaleBinding = filter->uvScale;
	filter->uvOffsetBinding = filter->uvOffset;
}

void ShaderFilter::videoRender(void *data, gs_effect_t *effect)
{
	ShaderFilter *filter = static_cast<ShaderFilter*>(data);
	size_t i;

	if (filter->effect != nullptr) {
		if (!obs_source_process_filter_begin(filter->context,
			GS_RGBA, OBS_NO_DIRECT_RENDERING))
			return;

		std::vector<ShaderParameter*> parameters = filter->parameters();

		for (i = 0; i < parameters.size(); i++) {
			if (parameters[i])
				parameters[i]->videoRender(filter);
		}
		
		obs_source_process_filter_end(filter->context, filter->effect,
				filter->total_width, filter->total_height);
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
		if (parameters[i])
			parameters[i]->update(filter);
	}
}

obs_properties_t *ShaderFilter::getProperties(void *data)
{
	ShaderFilter *filter = static_cast<ShaderFilter*>(data);
	size_t i;
	obs_properties_t *props = obs_properties_create();
	obs_properties_set_param(props, filter, NULL);

	std::string shaderPath = obs_get_module_data_path(obs_current_module());
	shaderPath += "/shaders";

	obs_properties_add_button(props, "reload_effect",
		_MT("ShaderFilter.ReloadEffect"),
		shader_filter_reload_effect_clicked);

	obs_property_t *file_name = obs_properties_add_path(props,
			"shader_file_name", _MT("ShaderFilter.ShaderFileName"),
			OBS_PATH_FILE, NULL, shaderPath.c_str());

	obs_property_set_modified_callback(file_name,
			shader_filter_file_name_changed);

	std::vector<ShaderParameter*> parameters = filter->parameters();
	for (i = 0; i < parameters.size(); i++) {
		if (parameters[i])
			parameters[i]->getProperties(filter, props);
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
