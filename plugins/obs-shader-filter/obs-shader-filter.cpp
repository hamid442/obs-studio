#include "obs-shader-filter.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs_shader_filter", "en-US")
#define blog(level, msg, ...) blog(level, "shader-filter: " msg, ##__VA_ARGS__)

static void sidechain_capture(void *p, obs_source_t *source, const struct audio_data *audio_data, bool muted);

static bool shader_filter_reload_effect_clicked(obs_properties_t *props, obs_property_t *property, void *data);

static bool shader_filter_file_name_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *settings);

static const char *shader_filter_texture_file_filter = "Textures (*.bmp *.tga *.png *.jpeg *.jpg *.gif);;";

static const char *shader_filter_media_file_filter =
		"Video Files (*.mp4 *.ts *.mov *.wmv *.flv *.mkv *.avi *.gif *.webm);;";

#define M_PI_D 3.141592653589793238462643383279502884197169399375
static double hlsl_clamp(double in, double min, double max)
{
	if (in < min)
		return min;
	if (in > max)
		return max;
	return in;
}

static double hlsl_degrees(double radians)
{
	return radians * (180.0 / M_PI_D);
}

static double hlsl_rad(double degrees)
{
	return degrees * (M_PI_D / 180.0);
}

static double audio_mel_from_hz(double hz)
{
	return 2595 * log10(1 + hz / 700.0);
}

static double audio_hz_from_mel(double mel)
{
	return 700 * (pow(10, mel / 2595) - 1);
}

const static double flt_max = FLT_MAX;
const static double flt_min = FLT_MIN;
const static double int_min = INT_MIN;
const static double int_max = INT_MAX;
static double       sample_rate;
static double       output_channels;
static std::string  dir[4] = {"left", "right", "top", "bottom"};

/* Additional likely to be used functions for mathmatical expressions */
void prepFunctions(std::vector<te_variable> *vars, ShaderFilter *filter)
{
	UNUSED_PARAMETER(filter);
	std::vector<te_variable> funcs = {{"clamp", hlsl_clamp, TE_FUNCTION3}, {"float_max", &flt_max},
			{"float_min", &flt_min}, {"int_max", &int_max}, {"int_min", &int_min},
			{"sample_rate", &sample_rate}, {"channels", &output_channels},
			{"mel_from_hz", audio_mel_from_hz, TE_FUNCTION1},
			{"hz_from_mel", audio_hz_from_mel, TE_FUNCTION1}, {"degrees", hlsl_degrees, TE_FUNCTION1},
			{"radians", hlsl_rad, TE_FUNCTION1}, {"random", random_double, TE_FUNCTION2},
			{"mouse_pos_x", &filter->_mouseX}, {"mouse_pos_y", &filter->_mouseY},
			{"mouse_type", &filter->_mouseType}, {"mouse_wheel_delta_x", &filter->_mouseWheelDeltaX},
			{"mouse_wheel_delta_y", &filter->_mouseWheelDeltaY}, {"mouse_wheel_x", &filter->_mouseWheelX},
			{"mouse_wheel_y", &filter->_mouseWheelY}, {"mouse_leave", &filter->_mouseLeave},
			{"mouse_up", &filter->_mouseUp}, {"mouse_click_x", &filter->_mouseClickX},
			{"mouse_click_y", &filter->_mouseClickY}, {"key", &filter->_key},
			{"key_pressed", &filter->_keyUp}};
	vars->reserve(vars->size() + funcs.size());
	vars->insert(vars->end(), funcs.begin(), funcs.end());
}

std::string toSnakeCase(std::string str)
{
	size_t i;
	char   c;
	for (i = 0; i < str.size(); i++) {
		c = str[i];
		if (isupper(c)) {
			str.insert(i++, "_");
			str.assign(i, (char)tolower(c));
		}
	}
	return str;
}

std::string toCamelCase(std::string str)
{
	size_t i;
	char   c;
	for (i = 0; i < str.size(); i++) {
		c = str[i];
		if (c == '_') {
			str.erase(i);
			if (i < str.size())
				str.assign(i, (char)toupper(c));
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
	int   defaultInt   = 0;

	void *               data = nullptr;
	size_t               size = 0;
	gs_shader_param_type type = GS_SHADER_PARAM_UNKNOWN;
	EVal(){};
	~EVal()
	{
		if (data)
			bfree(data);
	};

	operator std::vector<float>()
	{
		std::vector<float> d_float;
		std::vector<int>   d_int;
		std::vector<bool>  d_bool;
		float *            ptr_float = static_cast<float *>(data);
		int *              ptr_int   = static_cast<int *>(data);
		bool *             ptr_bool  = static_cast<bool *>(data);

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
				d_float.push_back((float)d_int[i]);
			break;
		}
		return d_float;
	}

	operator std::vector<int>()
	{
		std::vector<float> d_float;
		std::vector<int>   d_int;
		std::vector<bool>  d_bool;
		float *            ptr_float = static_cast<float *>(data);
		int *              ptr_int   = static_cast<int *>(data);
		bool *             ptr_bool  = static_cast<bool *>(data);

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
				d_int.push_back((int)d_float[i]);
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
		std::vector<int>   d_int;
		std::vector<bool>  d_bool;
		float *            ptr_float = static_cast<float *>(data);
		int *              ptr_int   = static_cast<int *>(data);
		bool *             ptr_bool  = static_cast<bool *>(data);

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
		std::string str      = "";
		char *      ptr_char = static_cast<char *>(data);

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
		return ((std::string) * this).c_str();
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

			v       = new EVal();
			v->data = gs_effect_get_default_val(eparam);
			v->size = gs_effect_get_default_val_size(eparam);
			v->type = note_info.type;
		}

		return v;
	}

protected:
	gs_eparam_t *                             _param      = nullptr;
	gs_effect_param_info                      _param_info = {0};
	EVal *                                    _value      = nullptr;
	std::unordered_map<std::string, EParam *> _annotations_map;
	size_t                                    _annotationCount;

public:
	std::unordered_map<std::string, EParam *> *getAnnootations()
	{
		return &_annotations_map;
	}

	gs_effect_param_info info() const
	{
		return _param_info;
	}

	EVal *getValue()
	{
		return _value ? _value : (_value = getValue(_param));
	}

	gs_eparam_t *getParam()
	{
		return _param;
	}

	operator gs_eparam_t *()
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

	EParam *operator[](std::string name)
	{
		return getAnnotation(name);
	}

	EVal *getAnnotationValue(std::string name)
	{
		EParam *note = getAnnotation(name);
		if (note)
			return note->getValue();
		else
			return nullptr;
	}

	template<class DataType> std::vector<DataType> getAnnotationValue(std::string name)
	{
		std::vector<DataType> ret;
		EParam *              note = getAnnotation(name);
		if (note)
			ret = *(note->getValue());
		return ret;
	}

	template<class DataType> DataType getAnnotationValue(std::string name, DataType defaultValue, int index = 0)
	{
		std::vector<DataType> ret;
		EParam *              note = getAnnotation(name);
		if (note)
			ret = *(note->getValue());
		if (index < ret.size())
			return ret[index];
		else
			return defaultValue;
	}

	bool hasAnnotation(std::string name)
	{
		return _annotations_map.find(name) != _annotations_map.end();
	}

	EParam(gs_eparam_t *param)
	{
		_param = param;
		gs_effect_get_param_info(param, &_param_info);
		_value = getValue(param);

		size_t i;
		_annotationCount = gs_param_get_num_annotations(_param);
		_annotations_map.reserve(_annotationCount);

		gs_eparam_t *                               p = nullptr;
		std::vector<EParam *>::iterator             annotation_it;
		std::vector<gs_effect_param_info>::iterator info_it;

		for (i = 0; i < _annotationCount; i++) {
			p                       = gs_param_get_annotation_by_idx(_param, i);
			EParam *             ep = new EParam(p);
			gs_effect_param_info _info;
			gs_effect_get_param_info(p, &_info);

			_annotations_map.insert(std::pair<std::string, EParam *>(_info.name, ep));
		}
	}

	~EParam()
	{
		if (_value)
			delete _value;
		for (const auto &annotation : _annotations_map)
			delete annotation.second;
		_annotations_map.clear();
	}

	template<class DataType> void setValue(DataType *data, size_t size)
	{
		size_t len       = size / sizeof(DataType);
		size_t arraySize = len * sizeof(DataType);
		gs_effect_set_val(_param, data, arraySize);
	}

	template<class DataType> void setValue(std::vector<DataType> data)
	{
		size_t arraySize = data.size() * sizeof(DataType);
		gs_effect_set_val(_param, data.data(), arraySize);
	}
};

class ShaderData {
protected:
	gs_shader_param_type _paramType;

	ShaderFilter *   _filter;
	ShaderParameter *_parent;
	EParam *         _param;

	std::vector<out_shader_data> _values;
	std::vector<in_shader_data>  _bindings;

	std::vector<std::string> _names;
	std::vector<std::string> _descs;
	std::vector<std::string> _tooltips;
	std::vector<std::string> _binding_names;
	std::vector<std::string> _expressions;

	size_t _dataCount;

public:
	gs_shader_param_type getParamType() const
	{
		return _paramType;
	}

	ShaderParameter *getParent()
	{
		return _parent;
	}

	ShaderData(ShaderParameter *parent = nullptr, ShaderFilter *filter = nullptr) : _parent(parent), _filter(filter)
	{
		if (_parent)
			_param = _parent->getParameter();
		else
			_param = nullptr;
	}

	virtual ~ShaderData(){};

	virtual void init(gs_shader_param_type paramType)
	{
		_paramType = paramType;
		_dataCount = getDataSize(paramType);

		_names.reserve(_dataCount);
		_descs.reserve(_dataCount);
		_values.reserve(_dataCount);
		_bindings.reserve(_dataCount);
		_expressions.reserve(_dataCount);
		_binding_names.reserve(_dataCount);
		_tooltips.reserve(_dataCount);

		size_t          i;
		out_shader_data empty        = {0};
		in_shader_data  emptyBinding = {0};

		std::string n      = _parent->getName();
		std::string d      = _parent->getDescription();
		std::string strNum = "";
		EVal *      val    = nullptr;
		for (i = 0; i < _dataCount; i++) {
			if (_dataCount > 1)
				strNum = "_" + std::to_string(i);
			_names.push_back(n + strNum);
			val = _param->getAnnotationValue("desc" + strNum);
			if (val)
				_descs.push_back(*val);
			else
				_descs.push_back(d + strNum);
			_binding_names.push_back(toSnakeCase(_names[i]));
			val = _param->getAnnotationValue("tooltiop" + strNum);
			if (val)
				_tooltips.push_back(*val);
			else
				_tooltips.push_back(_binding_names[i]);
			_values.push_back(empty);
			_bindings.push_back(emptyBinding);

			val = _param->getAnnotationValue("expr" + strNum);
			if (val)
				_expressions.push_back(*val);
			else
				_expressions.push_back("");
		}

		for (i = 0; i < 4; i++) {
			if (_filter->resizeExpressions[i].empty()) {
				val = _param->getAnnotationValue("resize_expr_" + dir[i]);
				if (val)
					_filter->resizeExpressions[i] = val->getString();
			}
		}
	};

	virtual void getProperties(ShaderFilter *filter, obs_properties_t *props)
	{
		UNUSED_PARAMETER(filter);
		UNUSED_PARAMETER(props);
	};

	virtual void videoTick(ShaderFilter *filter, float elapsedTime, float seconds)
	{
		UNUSED_PARAMETER(filter);
		UNUSED_PARAMETER(elapsedTime);
		UNUSED_PARAMETER(seconds);
	};

	virtual void videoRender(ShaderFilter *filter)
	{
		UNUSED_PARAMETER(filter);
	};

	virtual void update(ShaderFilter *filter)
	{
		UNUSED_PARAMETER(filter);
	};

	virtual void onPass(ShaderFilter *filter, const char *technique, size_t pass, gs_texture_t *texture)
	{
		UNUSED_PARAMETER(filter);
		UNUSED_PARAMETER(technique);
		UNUSED_PARAMETER(pass);
		UNUSED_PARAMETER(texture);
	}

	virtual void onTechniqueEnd(ShaderFilter *filter, const char *technique, gs_texture_t *texture)
	{
		UNUSED_PARAMETER(filter);
		UNUSED_PARAMETER(technique);
		UNUSED_PARAMETER(texture);
	}
};

class NumericalData : public ShaderData {
private:
	void fillIntList(EParam *e, obs_property_t *p)
	{
		std::unordered_map<std::string, EParam *> *notations = e->getAnnootations();
		for (std::unordered_map<std::string, EParam *>::iterator it = notations->begin();
				it != notations->end(); it++) {
			EParam *    eparam = (*it).second;
			EVal *      eval   = eparam->getValue();
			std::string name   = eparam->info().name;

			if (name.compare(0, 9, "list_item") == 0 && name.compare(name.size() - 6, 5, "_name") != 0) {
				std::vector<int> iList = *eval;
				if (iList.size()) {
					EVal *      evalname = e->getAnnotationValue((name + "_name"));
					std::string itemname = *evalname;
					int         d        = iList[0];
					if (itemname.empty())
						itemname = std::to_string(d);
					obs_property_list_add_int(p, itemname.c_str(), d);
				}
			}
		}
	}

	void fillFloatList(EParam *e, obs_property_t *p)
	{
		std::unordered_map<std::string, EParam *> *notations = e->getAnnootations();
		for (std::unordered_map<std::string, EParam *>::iterator it = notations->begin();
				it != notations->end(); it++) {
			EParam *    eparam = (*it).second;
			EVal *      eval   = eparam->getValue();
			std::string name   = eparam->info().name;
			// gs_shader_param_type type   = eparam->info().type;

			if (name.compare(0, 9, "list_item") == 0 && name.compare(name.size() - 6, 5, "_name") != 0) {
				std::vector<float> fList = *eval;
				if (fList.size()) {
					EVal *      evalname = e->getAnnotationValue((name + "_name"));
					std::string itemname = *evalname;
					double      d        = fList[0];
					if (itemname.empty())
						itemname = std::to_string(d);
					obs_property_list_add_float(p, itemname.c_str(), d);
				}
			}
		}
	}

	void fillComboBox(EParam *e, obs_property_t *p)
	{
		EVal *      enabledval  = e->getAnnotationValue("enabled_desc");
		EVal *      disabledval = e->getAnnotationValue("disabled_desc");
		std::string enabled     = _OMT("On");
		std::string disabled    = _OMT("Off");
		if (enabledval) {
			std::string temp = *enabledval;
			if (!temp.empty())
				enabled = temp;
		}
		if (disabledval) {
			std::string temp = *disabledval;
			if (!temp.empty())
				disabled = temp;
		}
		obs_property_list_add_int(p, enabled.c_str(), 1);
		obs_property_list_add_int(p, disabled.c_str(), 0);
	}

protected:
	bool              _isFloat;
	bool              _isInt;
	bool              _isSlider;
	bool              _skipWholeProperty;
	bool              _skipCalculations;
	bool              _showExpressionLess;
	std::vector<bool> _skipProperty;
	std::vector<bool> _disableProperty;
	double            _min;
	double            _max;
	double            _step;
	enum BindType { unspecified, none, byte, short_integer, integer, floating_point, double_point };
	void *   _bind = nullptr;
	BindType bindType;
	enum NumericalType { combobox, list, num, slider, color };
	NumericalType _numType;

public:
	NumericalData(ShaderParameter *parent, ShaderFilter *filter) : ShaderData(parent, filter)
	{
		gs_eparam_t *               param = parent->getParameter()->getParam();
		struct gs_effect_param_info info;
		gs_effect_get_param_info(param, &info);
		/* std::vector<DataType> *bind */
		std::string n = info.name;
		if (n == "ViewProj") {
			bindType = floating_point;
			_bind    = &_filter->view_proj;
		} else if (n == "uv_offset") {
			bindType = floating_point;
			_bind    = &_filter->uvOffset;
		} else if (n == "uv_scale") {
			bindType = floating_point;
			_bind    = &_filter->uvScale;
		} else if (n == "uv_pixel_interval") {
			bindType = floating_point;
			_bind    = &_filter->uvPixelInterval;
		} else if (n == "elapsed_time") {
			bindType = floating_point;
			_bind    = &_filter->elapsedTime;
		}
	};

	~NumericalData(){};

	void init(gs_shader_param_type paramType)
	{
		ShaderData::init(paramType);
		_isFloat           = isFloatType(paramType);
		_isInt             = isIntType(paramType);
		_skipWholeProperty = _bind ? true : false;
		_skipCalculations  = false;
		size_t i;
		if (_isFloat) {
			_min  = _param->getAnnotationValue<float>("min", -FLT_MAX);
			_max  = _param->getAnnotationValue<float>("max", FLT_MAX);
			_step = _param->getAnnotationValue<float>("step", 1.0);
		} else {
			_min  = _param->getAnnotationValue<int>("min", INT_MIN);
			_max  = _param->getAnnotationValue<int>("max", INT_MAX);
			_step = _param->getAnnotationValue<int>("step", 1.0);
		}

		EVal *guitype  = _param->getAnnotationValue("type");
		bool  isSlider = _param->getAnnotationValue<bool>("is_slider", true);

		std::unordered_map<std::string, uint32_t> types = {{"combobox", combobox}, {"list", list}, {"num", num},
				{"slider", slider}, {"color", color}};

		_numType = num;
		if (guitype && types.find(guitype->getString()) != types.end()) {
			_numType = (NumericalType)types.at(guitype->getString());
		} else if (isSlider) {
			_numType = slider;
		}

		_disableProperty.reserve(_dataCount);
		_skipProperty.reserve(_dataCount);

		for (i = 0; i < _dataCount; i++) {
			te_variable var = {0};
			var.address     = &_bindings[i];
			var.name        = _binding_names[i].c_str();
			if (_filter)
				_filter->appendVariable(var);
		}

		bool hasExpressions = false;
		for (i = 0; i < _expressions.size(); i++) {
			if (_expressions[i].empty()) {
				_disableProperty.push_back(false);
				_skipProperty.push_back(false);
				continue;
			}
			hasExpressions = true;
			_filter->compileExpression(_expressions[i]);
			if (_filter->expressionCompiled()) {
				_disableProperty.push_back(false);
				_skipProperty.push_back(true);
			} else {
				_disableProperty.push_back(true);
				_skipProperty.push_back(false);
				_tooltips[i] = _filter->expressionError();
			}
		}

		bool showExprLess = _param->getAnnotationValue("show_exprless", false);
		if (!showExprLess)
			_showExpressionLess = !hasExpressions;
		else
			_showExpressionLess = showExprLess;
	}

	void getProperties(ShaderFilter *filter, obs_properties_t *props)
	{
		UNUSED_PARAMETER(filter);
		size_t i;
		if (_bind || _skipWholeProperty)
			return;
		obs_property_t *p;
		if (_isFloat) {
			if (_numType == color && _dataCount == 4) {
				obs_properties_add_color(props, _names[0].c_str(), _descs[0].c_str());
				return;
			}
			for (i = 0; i < _dataCount; i++) {
				if (_skipProperty[i])
					continue;
				if (!_showExpressionLess && _expressions[i].empty())
					continue;
				switch (_numType) {
				case combobox:
				case list:
					p = obs_properties_add_list(props, _names[i].c_str(), _descs[i].c_str(),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_FLOAT);
					fillFloatList(_param, p);
					break;
				case slider:
					p = obs_properties_add_float_slider(
							props, _names[i].c_str(), _descs[i].c_str(), _min, _max, _step);
					break;
				default:
					p = obs_properties_add_float(
							props, _names[i].c_str(), _descs[i].c_str(), _min, _max, _step);
					break;
				}
				obs_property_set_enabled(p, !_disableProperty[i]);
				obs_property_set_long_description(p, _tooltips[i].c_str());
			}
		} else if (_isInt) {
			for (i = 0; i < _dataCount; i++) {
				if (_skipProperty[i])
					continue;
				if (!_showExpressionLess && _expressions[i].empty())
					continue;
				switch (_numType) {
				case combobox:
				case list:
					p = obs_properties_add_list(props, _names[i].c_str(), _descs[i].c_str(),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
					fillIntList(_param, p);
					break;
				case slider:
					p = obs_properties_add_int_slider(props, _names[i].c_str(), _descs[i].c_str(),
							(int)_min, (int)_max, (int)_step);
					break;
				default:
					p = obs_properties_add_int(props, _names[i].c_str(), _descs[i].c_str(),
							(int)_min, (int)_max, (int)_step);
					break;
				}
				obs_property_set_enabled(p, !_disableProperty[i]);
				obs_property_set_long_description(p, _tooltips[i].c_str());
			}
		} else {
			for (i = 0; i < _dataCount; i++) {
				if (_skipProperty[i])
					continue;
				if (!_showExpressionLess && _expressions[i].empty())
					continue;
				switch (_numType) {
				case combobox:
				case list:
					p = obs_properties_add_list(props, _names[i].c_str(), _descs[i].c_str(),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
					fillComboBox(_param, p);
					break;
				default:
					p = obs_properties_add_bool(props, _names[i].c_str(), _descs[i].c_str());
				}
				obs_property_set_enabled(p, !_disableProperty[i]);
				obs_property_set_long_description(p, _tooltips[i].c_str());
			}
		}
	}

	void update(ShaderFilter *filter)
	{
		if (_bind || _skipWholeProperty)
			return;
		obs_data_t *settings = filter->getSettings();
		size_t      i;
		for (i = 0; i < _dataCount; i++) {
			switch (_paramType) {
			case GS_SHADER_PARAM_BOOL:
				switch (_numType) {
				case combobox:
				case list:
					_bindings[i].s64i = obs_data_get_int(settings, _names[i].c_str());
					_values[i].s32i   = (int32_t)_bindings[i].s64i;
					break;
				default:
					_bindings[i].s64i = obs_data_get_bool(settings, _names[i].c_str());
					_values[i].s32i   = (int32_t)_bindings[i].s64i;
					break;
				}
				break;
			case GS_SHADER_PARAM_INT:
			case GS_SHADER_PARAM_INT2:
			case GS_SHADER_PARAM_INT3:
			case GS_SHADER_PARAM_INT4:
				_bindings[i].s64i = obs_data_get_int(settings, _names[i].c_str());
				_values[i].s32i   = (int32_t)_bindings[i].s64i;
				break;
			case GS_SHADER_PARAM_FLOAT:
			case GS_SHADER_PARAM_VEC2:
			case GS_SHADER_PARAM_VEC3:
			case GS_SHADER_PARAM_VEC4:
			case GS_SHADER_PARAM_MATRIX4X4:
				_bindings[i].d = obs_data_get_double(settings, _names[i].c_str());
				_values[i].f   = (float)_bindings[i].d;
				break;
			default:
				break;
			}
		}
	}

	void videoTick(ShaderFilter *filter, float elapsedTime, float seconds)
	{
		UNUSED_PARAMETER(seconds);
		UNUSED_PARAMETER(elapsedTime);
		size_t i;
		if (_skipCalculations)
			return;
		for (i = 0; i < _dataCount; i++) {
			if (!_expressions[i].empty()) {
				switch (_paramType) {
				case GS_SHADER_PARAM_BOOL:
					_filter->compileExpression(_expressions[i]);
					_bindings[i].s64i = filter->evaluateExpression<long long>(0);
					_values[i].s32i   = (int32_t)_bindings[i].s64i;
					break;
				case GS_SHADER_PARAM_INT:
				case GS_SHADER_PARAM_INT2:
				case GS_SHADER_PARAM_INT3:
				case GS_SHADER_PARAM_INT4:
					_filter->compileExpression(_expressions[i]);
					_bindings[i].s64i = filter->evaluateExpression<long long>(0);
					_values[i].s32i   = (int32_t)_bindings[i].s64i;
					break;
				case GS_SHADER_PARAM_FLOAT:
				case GS_SHADER_PARAM_VEC2:
				case GS_SHADER_PARAM_VEC3:
				case GS_SHADER_PARAM_VEC4:
				case GS_SHADER_PARAM_MATRIX4X4:
					_filter->compileExpression(_expressions[i]);
					_bindings[i].d = filter->evaluateExpression<double>(0);
					_values[i].f   = (float)_bindings[i].d;
					break;
				default:
					break;
				}
			} else if (_bind) {
				switch (_paramType) {
				case GS_SHADER_PARAM_BOOL:
					_bindings[i].s64i = static_cast<bool *>(_bind)[i];
					_values[i].s32i   = (int32_t)_bindings[i].s64i;
					break;
				case GS_SHADER_PARAM_INT:
				case GS_SHADER_PARAM_INT2:
				case GS_SHADER_PARAM_INT3:
				case GS_SHADER_PARAM_INT4:
					_bindings[i].s64i = static_cast<int *>(_bind)[i];
					_values[i].s32i   = (int32_t)_bindings[i].s64i;
					break;
				case GS_SHADER_PARAM_FLOAT:
				case GS_SHADER_PARAM_VEC2:
				case GS_SHADER_PARAM_VEC3:
				case GS_SHADER_PARAM_VEC4:
				case GS_SHADER_PARAM_MATRIX4X4:
					_bindings[i].d = static_cast<float *>(_bind)[i];
					_values[i].f   = (float)_bindings[i].d;
					break;
				default:
					break;
				}
			}
		}
	}

	void setData()
	{
		if (_param) {
			if (_isFloat) {
				float *data = (float *)_values.data();
				_param->setValue<float>(data, _values.size() * sizeof(float));
			} else {
				int *data = (int *)_values.data();
				_param->setValue<int>(data, _values.size() * sizeof(int));
			}
		}
	}

	template<class DataType> void setData(DataType t)
	{
		if (_param)
			_param->setValue<DataType>(&t, sizeof(t));
	}

	template<class DataType> void setData(std::vector<DataType> t)
	{
		if (_param)
			_param->setValue<DataType>(t.data(), t.size() * sizeof(DataType));
	}

	void videoRender(ShaderFilter *filter)
	{
		UNUSED_PARAMETER(filter);
		if (_skipCalculations)
			return;

		setData();
	}
};

/* TODO? */
class StringData : public ShaderData {
	std::string _value;

	std::vector<std::string> _binding;
	std::vector<double>      _bindings;

public:
	StringData(ShaderParameter *parent, ShaderFilter *filter) : ShaderData(parent, filter){};

	~StringData(){};

	void init(gs_shader_param_type paramType)
	{
		ShaderData::init(paramType);
	}
};

/* functions to add sources to a list for use as textures */
static bool fillPropertiesSourceList(void *param, obs_source_t *source)
{
	obs_property_t *p           = (obs_property_t *)param;
	uint32_t        flags       = obs_source_get_output_flags(source);
	const char *    source_name = obs_source_get_name(source);

	if ((flags & OBS_SOURCE_VIDEO) != 0 && obs_source_active(source))
		obs_property_list_add_string(p, source_name, source_name);

	return true;
}

static void fillSourceList(obs_property_t *p)
{
	obs_property_list_add_string(p, _OMT("None"), "");
	obs_enum_sources(&fillPropertiesSourceList, (void *)p);
}

static bool fillPropertiesAudioSourceList(void *param, obs_source_t *source)
{
	obs_property_t *p           = (obs_property_t *)param;
	uint32_t        flags       = obs_source_get_output_flags(source);
	const char *    source_name = obs_source_get_name(source);

	if ((flags & OBS_SOURCE_AUDIO) != 0 && obs_source_active(source))
		obs_property_list_add_string(p, source_name, source_name);

	return true;
}

static void fillAudioSourceList(obs_property_t *p)
{
	obs_property_list_add_string(p, _OMT("None"), "");
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

		_src_cx = media_cx;
		_src_cy = media_cy;

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
		size_t h_samples     = samples / 2;
		size_t h_sample_size = samples * 2;

		for (i = 0; i < _channels; i++) {
			audio_fft_complex(((float *)_data) + (i * samples), (uint32_t)samples);
		}
		for (i = 1; i < _channels; i++) {
			memcpy(((float *)_data) + (i * h_samples), ((float *)_data) + (i * samples), h_sample_size);
		}
		return (uint32_t)h_samples;
	}

	void renderAudioSource(EParam *param, uint64_t samples)
	{
		if (!_data)
			_data = (uint8_t *)bzalloc(_maxAudioSize * _channels * sizeof(float));
		size_t px_width = samples;
		audiolock();
		size_t i = 0;
		for (i = 0; i < _channels; i++) {
			if (_audio[i].data())
				memcpy((float *)_data + (samples * i), _audio[i].data(), samples * sizeof(float));
			else
				memset((float *)_data + (samples * i), 0, samples * sizeof(float));
		}
		audiounlock();

		if (_isFFT)
			px_width = processAudio(samples);

		_src_cx = px_width;
		_src_cy = _channels;
		obs_enter_graphics();
		gs_texture_destroy(_tex);
		_tex = gs_texture_create(
				(uint32_t)px_width, (uint32_t)_channels, GS_R32F, 1, (const uint8_t **)&_data, 0);
		obs_leave_graphics();
		gs_effect_set_texture(*param, _tex);
	}

	void updateAudioSource(std::string name)
	{
		if (!name.empty()) {
			obs_source_t *sidechain     = nullptr;
			sidechain                   = obs_get_source_by_name(name.c_str());
			obs_source_t *old_sidechain = _mediaSource;
			lock();
			if (old_sidechain) {
				obs_source_remove_audio_capture_callback(old_sidechain, sidechain_capture, this);
				obs_source_release(old_sidechain);
				for (size_t i = 0; i < MAX_AV_PLANES; i++)
					_audio[i].clear();
			}
			if (sidechain)
				obs_source_add_audio_capture_callback(sidechain, sidechain_capture, this);
			_mediaSource = sidechain;
			unlock();
		}
	}

	PThreadMutex *_mutex      = nullptr;
	PThreadMutex *_audioMutex = nullptr;

protected:
	gs_texrender_t *   _texrender = nullptr;
	gs_texture_t *     _tex       = nullptr;
	gs_image_file_t *  _image     = nullptr;
	std::vector<float> _audio[MAX_AV_PLANES];
	std::vector<float> _tempAudio[MAX_AV_PLANES];
	bool               _isFFT = false;
	std::vector<float> _fft_data[MAX_AV_PLANES];
	size_t             _channels     = 0;
	size_t             _maxAudioSize = AUDIO_OUTPUT_FRAMES * 2;
	uint8_t *          _data         = nullptr;
	obs_source_t *     _mediaSource  = nullptr;
	std::string        _sourceName   = "";
	size_t             _size;
	uint8_t            _range_0;
	uint8_t            _range_1;
	enum TextureType { ignored, unspecified, source, audio, image, media, random, buffer };
	fft_windowing_type _window;
	TextureType        _texType;
	std::string        _filePath;

	std::string _size_w_binding;
	std::string _size_h_binding;
	std::string _tech;
	size_t      _pass;
	double      _src_cx;
	double      _src_cy;

public:
	TextureData(ShaderParameter *parent, ShaderFilter *filter)
		: ShaderData(parent, filter), _maxAudioSize(AUDIO_OUTPUT_FRAMES * 2)
	{
		_maxAudioSize = AUDIO_OUTPUT_FRAMES * 2;
		_mutex        = new PThreadMutex();
		_audioMutex   = new PThreadMutex();
	};

	~TextureData()
	{
		if (_texType == audio) {
			obs_source_remove_audio_capture_callback(_mediaSource, sidechain_capture, this);
		}
		if (_mediaSource)
			obs_source_release(_mediaSource);
		_mediaSource = nullptr;

		obs_enter_graphics();
		gs_texrender_destroy(_texrender);
		gs_image_file_free(_image);
		if (_tex)
			gs_texture_destroy(_tex);
		obs_leave_graphics();
		_texrender = nullptr;
		_tex       = nullptr;
		if (_image)
			bfree(_image);
		_image = nullptr;

		if (_data)
			bfree(_data);
		if (_mutex)
			delete _mutex;
		if (_audioMutex)
			delete _audioMutex;
	};

	void lock()
	{
		_mutex->lock();
	}

	void unlock()
	{
		_mutex->unlock();
	}

	void audiolock()
	{
		_audioMutex->lock();
	}

	void audiounlock()
	{
		_audioMutex->unlock();
	}

	size_t getAudioChannels()
	{
		return _channels;
	}

	void insertAudio(float *data, size_t samples, size_t index)
	{
		if (!samples || index > (MAX_AV_PLANES - 1))
			return;
		audiolock();
		size_t old_size    = _audio[index].size() * sizeof(float);
		size_t insert_size = samples * sizeof(float);
		float *old_data    = nullptr;
		if (old_size)
			old_data = (float *)bmemdup(_audio[index].data(), old_size);
		_audio[index].resize(_maxAudioSize);
		if (samples < _maxAudioSize) {
			if (old_data)
				memcpy(&_audio[index][samples], old_data, old_size - insert_size);
			if (data)
				memcpy(&_audio[index][0], data, insert_size);
			else
				memset(&_audio[index][0], 0, insert_size);
		} else {
			if (data)
				memcpy(&_audio[index][0], data, _maxAudioSize * sizeof(float));
			else
				memset(&_audio[index][0], 0, _maxAudioSize * sizeof(float));
		}
		audiounlock();
		bfree(old_data);
	}

	void init(gs_shader_param_type paramType)
	{
		_paramType = paramType;
		_names.push_back(_parent->getName());
		_descs.push_back(_parent->getDescription());

		EVal *                                    texType = _param->getAnnotationValue("texture_type");
		std::unordered_map<std::string, uint32_t> types   = {{"source", source}, {"audio", audio},
                                {"image", image}, {"media", media}, {"random", random}, {"buffer", buffer}};

		if (texType && types.find(texType->getString()) != types.end()) {
			_texType = (TextureType)types.at(texType->getString());
		} else {
			_texType = image;
		}

		if (_names[0] == "image")
			_texType = ignored;

		_channels = audio_output_get_channels(obs_get_audio());

		EVal *techAnnotion = _param->getAnnotationValue("technique");
		EVal *window       = nullptr; 
		switch (_texType) {
		case audio:
			_channels = _param->getAnnotationValue<int>("channels", 0);

			for (size_t i = 0; i < MAX_AV_PLANES; i++)
				_audio->resize(AUDIO_OUTPUT_FRAMES);

			_isFFT = _param->getAnnotationValue<bool>("is_fft", false);

			window = _param->getAnnotationValue("window");
			if (window)
				_window = get_window_type(window->c_str());
			else
				_window = none;
			break;
		case buffer:
			_tech = techAnnotion->getString();
			_pass = _param->getAnnotationValue<int>("pass", -1);
			break;
		default:
			break;
		}

		_binding_names.push_back(toSnakeCase(_names[0]));
		_size_w_binding = _binding_names[0] + "_w";
		_size_h_binding = _binding_names[0] + "_h";

		te_variable size_w = {0};
		size_w.address     = &_src_cx;
		size_w.name        = _size_w_binding.c_str();
		te_variable size_h = {0};
		size_h.address     = &_src_cy;
		size_h.name        = _size_h_binding.c_str();

		if (_filter) {
			_filter->appendVariable(size_w);
			_filter->appendVariable(size_h);
		}
	}

	void getProperties(ShaderFilter *filter, obs_properties_t *props)
	{
		UNUSED_PARAMETER(filter);
		obs_property_t *p = nullptr;
		switch (_texType) {
		case source:
			p = obs_properties_add_list(props, _names[0].c_str(), _descs[0].c_str(), OBS_COMBO_TYPE_LIST,
					OBS_COMBO_FORMAT_STRING);
			fillSourceList(p);
			for (size_t i = 0; i < obs_property_list_item_count(p); i++) {
				std::string l = obs_property_list_item_string(p, i);
				std::string src = obs_source_get_name(_filter->context);
				if (l == src)
					obs_property_list_item_remove(p, i--);
				obs_source_t *parent = obs_filter_get_parent(_filter->context);
				std::string   parentName = "";
				if (parent)
					parentName = obs_source_get_name(parent);
				if (!parentName.empty() && l == parentName)
					obs_property_list_item_remove(p, i--);
			}
			break;
		case audio:
			p = obs_properties_add_list(props, _names[0].c_str(), _descs[0].c_str(), OBS_COMBO_TYPE_LIST,
					OBS_COMBO_FORMAT_STRING);
			fillAudioSourceList(p);
			for (size_t i = 0; i < obs_property_list_item_count(p); i++) {
				std::string l   = obs_property_list_item_string(p, i);
				std::string src = obs_source_get_name(_filter->context);
				if (l == src)
					obs_property_list_item_remove(p, i--);
				obs_source_t *parent     = obs_filter_get_parent(_filter->context);
				std::string   parentName = "";
				if (parent)
					parentName = obs_source_get_name(parent);
				if (!parentName.empty() && l == parentName)
					obs_property_list_item_remove(p, i--);
			}
			break;
		case media:
			p = obs_properties_add_path(props, _names[0].c_str(), _descs[0].c_str(), OBS_PATH_FILE,
					shader_filter_media_file_filter, NULL);
			break;
		case image:
			p = obs_properties_add_path(props, _names[0].c_str(), _descs[0].c_str(), OBS_PATH_FILE,
					shader_filter_texture_file_filter, NULL);
			break;
		case random:
			obs_properties_add_int(props, (_names[0] + "_range_0").c_str(), _descs[0].c_str(), 0, 255, 1);
			obs_properties_add_int(props, (_names[0] + "_range_1").c_str(), _descs[0].c_str(), 0, 255, 1);
			break;
		}
	}

	void update(ShaderFilter *filter)
	{
		obs_data_t *settings = filter->getSettings();
		_channels            = audio_output_get_channels(obs_get_audio());
		const char *file_path;
		switch (_texType) {
		case source:
			if (!_texrender)
				_texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
			obs_source_release(_mediaSource);
			_mediaSource = obs_get_source_by_name(obs_data_get_string(settings, _names[0].c_str()));
			break;
		case audio:
			updateAudioSource(obs_data_get_string(settings, _names[0].c_str()));
			break;
		case image:
			if (!_image) {
				_image = (gs_image_file_t *)bzalloc(sizeof(gs_image_file_t));
			} else {
				obs_enter_graphics();
				gs_image_file_free(_image);
				obs_leave_graphics();
			}

			file_path = obs_data_get_string(settings, _names[0].c_str());
			_filePath = file_path;
			if (file_path && file_path[0] != '\0') {
				gs_image_file_init(_image, file_path);
				obs_enter_graphics();
				gs_image_file_init_texture(_image);
				obs_leave_graphics();
			}
			break;
		case random:
			_range_0 = (uint8_t)obs_data_get_int(settings, (_names[0] + "_range_0").c_str());
			_range_1 = (uint8_t)obs_data_get_int(settings, (_names[0] + "_range_1").c_str());
			break;
		}
	}

	void videoTick(ShaderFilter *filter, float elapsedTime, float seconds)
	{
		UNUSED_PARAMETER(seconds);
		UNUSED_PARAMETER(elapsedTime);
		gs_texture_t *t;
		obs_enter_graphics();
		switch (_texType) {
		case media:
		case source:
			break;
		case audio:
			break;
		case image:
			t = _image ? _image->texture : NULL;
			if (t) {
				_src_cx = gs_texture_get_height(t);
				_src_cy = gs_texture_get_width(t);
			} else {
				_src_cx = 0;
				_src_cy = 0;
			}
			break;
		case random:
		case ignored:
			_src_cx = obs_source_get_width(filter->context);
			_src_cy = obs_source_get_height(filter->context);
			break;
		default:
			break;
		}
		obs_leave_graphics();
	}

	void videoRender(ShaderFilter *filter)
	{
		ShaderData::videoRender(filter);
		uint32_t      src_cx = obs_source_get_width(filter->context);
		uint32_t      src_cy = obs_source_get_height(filter->context);
		gs_texture_t *t;
		size_t        pixels;
		size_t        i;
		uint8_t       u;
		switch (_texType) {
		case media:
		case source:
			renderSource(_param, src_cx, src_cy);
			break;
		case audio:
			renderAudioSource(_param, AUDIO_OUTPUT_FRAMES);
			break;
		case image:
			t = _image ? _image->texture : NULL;
			if (_param)
				_param->setValue<gs_texture_t *>(&t, sizeof(gs_texture_t *));
			break;
		case random:
			pixels = src_cy * src_cx;
			if (!_data)
				_data = (uint8_t *)bmalloc(pixels);

			if (_range_0 < _range_1) {
				for (i = 0; i < pixels; i++)
					_data[i] = (uint8_t)random_int(_range_0, _range_1);
			} else {
				for (i = 0; i < pixels; i++) {
					u = random_int(0, _range_1 + (255 - _range_0));
					if (u > _range_1)
						u += _range_1 - _range_0;
					_data[i] = u;
				}
			}

			obs_enter_graphics();
			gs_texture_destroy(_tex);
			_tex = gs_texture_create(
					(uint32_t)src_cx, (uint32_t)src_cy, GS_R8, 1, (const uint8_t **)&_data, 0);
			obs_leave_graphics();
			gs_effect_set_texture(*_param, _tex);
			break;
		case buffer:
			_param->setValue<gs_texture_t *>(&_tex, sizeof(gs_texture *));
			break;
		default:
			break;
		}
	}

	void onPass(ShaderFilter *filter, const char *technique, size_t pass, gs_texture_t *texture)
	{
		if (_texType == buffer) {
			std::string tech = technique;
			if (tech == _tech && pass == _pass) {
				obs_enter_graphics();
				gs_texture_destroy(_tex);
				gs_copy_texture(_tex, texture);
				obs_enter_graphics();
			}
		}
	}

	void onTechniqueEnd(ShaderFilter *filter, const char *technique, gs_texture_t *texture)
	{
		if (_texType == buffer) {
			std::string tech = technique;
			if (tech == _tech && _pass == -1) {
				obs_enter_graphics();
				gs_texture_destroy(_tex);
				gs_copy_texture(_tex, texture);
				obs_enter_graphics();
			}
		}
	}
};

static void sidechain_capture(void *p, obs_source_t *source, const struct audio_data *audio_data, bool muted)
{
	TextureData *data = static_cast<TextureData *>(p);
	UNUSED_PARAMETER(source);
	if (!audio_data->frames)
		return;
	size_t i;
	if (muted) {
		for (i = 0; i < data->getAudioChannels(); i++)
			data->insertAudio(nullptr, audio_data->frames, i);
	} else {
		for (i = 0; i < data->getAudioChannels(); i++)
			data->insertAudio((float *)audio_data->data[i], audio_data->frames, i);
	}
}

class NullData : public ShaderData {
public:
	NullData(ShaderParameter *parent, ShaderFilter *filter) : ShaderData(parent, filter){};
	~NullData(){};
	void init(gs_shader_param_type paramType)
	{
		UNUSED_PARAMETER(paramType);
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

gs_shader_param_type ShaderParameter::getParameterType()
{
	return _paramType;
}

ShaderParameter::ShaderParameter(gs_eparam_t *param, ShaderFilter *filter) : _filter(filter)
{
	struct gs_effect_param_info info;
	gs_effect_get_param_info(param, &info);

	_mutex       = new PThreadMutex();
	_name        = info.name;
	_description = info.name;
	_param       = new EParam(param);

	init(info.type);
}

void ShaderParameter::init(gs_shader_param_type paramType)
{
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
		_shaderData->init(paramType);
}

ShaderParameter::~ShaderParameter()
{
	if (_param)
		delete _param;
	_param = nullptr;

	if (_shaderData)
		delete _shaderData;
	_shaderData = nullptr;

	if (_mutex)
		delete _mutex;
	_mutex = nullptr;
}

void ShaderParameter::lock()
{
	if (_mutex)
		_mutex->lock();
}

void ShaderParameter::unlock()
{
	if (_mutex)
		_mutex->unlock();
}

void ShaderParameter::videoTick(ShaderFilter *filter, float elapsed_time, float seconds)
{
	if (_shaderData)
		_shaderData->videoTick(filter, elapsed_time, seconds);
}

void ShaderParameter::videoRender(ShaderFilter *filter)
{
	if (_shaderData)
		_shaderData->videoRender(filter);
}

void ShaderParameter::update(ShaderFilter *filter)
{
	if (_shaderData)
		_shaderData->update(filter);
}

void ShaderParameter::getProperties(ShaderFilter *filter, obs_properties_t *props)
{
	if (_shaderData)
		_shaderData->getProperties(filter, props);
}

void ShaderParameter::onPass(ShaderFilter *filter, const char *technique, size_t pass, gs_texture_t *texture)
{
	if (_shaderData)
		_shaderData->onPass(filter, technique, pass, texture);
}

void ShaderParameter::onTechniqueEnd(ShaderFilter *filter, const char *technique, gs_texture_t *texture)
{
	if (_shaderData)
		_shaderData->onTechniqueEnd(filter, technique, texture);
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

std::vector<ShaderParameter *> ShaderFilter::parameters()
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
	blog(LOG_INFO, "appending %s", var.name);
}

void ShaderFilter::compileExpression(std::string expresion)
{
	expression.compile(expresion);
}

bool ShaderFilter::expressionCompiled()
{
	return expression;
}

std::string ShaderFilter::expressionError()
{
	return expression.errorString();
}

template<class DataType> DataType ShaderFilter::evaluateExpression(DataType default_value)
{
	return expression.evaluate(default_value);
}

ShaderFilter::ShaderFilter(obs_data_t *settings, obs_source_t *source)
{
	context   = source;
	_settings = settings;
	_mutex    = new PThreadMutex();
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

	obs_enter_graphics();
	gs_effect_destroy(effect);
	effect = nullptr;
	gs_texrender_destroy(filter_texrender);
	filter_texrender = nullptr;
	obs_leave_graphics();

	if (_mutex)
		delete _mutex;
};

void ShaderFilter::lock()
{
	if (_mutex)
		_mutex->lock();
}

void ShaderFilter::unlock()
{
	if (_mutex)
		_mutex->unlock();
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
	if (p) {
		paramList.push_back(p);
		paramMap.insert(std::pair<std::string, ShaderParameter *>(p->getName(), p));
		switch (p->getParameterType()) {
		case GS_SHADER_PARAM_TEXTURE:
			//textureList.push_back(p);
			break;
		}
		blog(LOG_INFO, "%s", p->getName().c_str());
	}
}

void ShaderFilter::reload()
{
	_reload_effect = false;
	size_t i;
	char * errors = NULL;

	/* Clear previous settings */
	while (!paramList.empty()) {
		ShaderParameter *p = paramList.back();
		paramList.pop_back();
		delete p;
		p = nullptr;
	}
	paramMap.clear();
	evaluationList.clear();
	expression.clear();

	prepFunctions(&expression, this);

	obs_enter_graphics();
	gs_effect_destroy(effect);
	effect = nullptr;
	obs_leave_graphics();

	_effect_path = obs_data_get_string(_settings, "shader_file_name");
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

	/* Create new parameters */
	size_t effect_count = gs_effect_get_num_params(effect);
	paramList.reserve(effect_count + paramList.size());
	paramMap.reserve(effect_count + paramList.size());
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
	delete static_cast<ShaderFilter *>(data);
}

const char *ShaderFilter::getName(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("ShaderFilter");
}

void ShaderFilter::videoTick(void *data, float seconds)
{
	ShaderFilter *filter = static_cast<ShaderFilter *>(data);
	filter->elapsedTimeBinding.d += seconds;
	filter->elapsedTime += seconds;

	size_t i;
	for (i = 0; i < filter->paramList.size(); i++) {
		if (filter->paramList[i])
			filter->paramList[i]->videoTick(filter, filter->elapsedTime, seconds);
	}

	int *resize[4] = {&filter->resizeLeft, &filter->resizeRight, &filter->resizeTop, &filter->resizeBottom};
	for (i = 0; i < 4; i++) {
		if (filter->resizeExpressions[i].empty())
			continue;
		filter->compileExpression(filter->resizeExpressions[i]);
		if (filter->expressionCompiled())
			*resize[i] = filter->evaluateExpression<int>(0);
	}

	obs_source_t *target = obs_filter_get_target(filter->context);
	/* Determine offsets from expansion values. */
	int baseWidth  = obs_source_get_base_width(target);
	int baseHeight = obs_source_get_base_height(target);

	filter->total_width  = filter->resizeLeft + baseWidth + filter->resizeRight;
	filter->total_height = filter->resizeTop + baseHeight + filter->resizeBottom;

	filter->uvScale.x         = (float)filter->total_width / baseWidth;
	filter->uvScale.y         = (float)filter->total_height / baseHeight;
	filter->uvOffset.x        = (float)(-filter->resizeLeft) / baseWidth;
	filter->uvOffset.y        = (float)(-filter->resizeTop) / baseHeight;
	filter->uvPixelInterval.x = 1.0f / baseWidth;
	filter->uvPixelInterval.y = 1.0f / baseHeight;

	filter->uvScaleBinding  = filter->uvScale;
	filter->uvOffsetBinding = filter->uvOffset;
}

void ShaderFilter::videoTickSource(void *data, float seconds)
{
	ShaderFilter *filter = static_cast<ShaderFilter *>(data);
	filter->elapsedTimeBinding.d += seconds;
	filter->elapsedTime += seconds;

	size_t i;
	for (i = 0; i < filter->paramList.size(); i++) {
		if (filter->paramList[i])
			filter->paramList[i]->videoTick(filter, filter->elapsedTime, seconds);
	}

	int *resize[4] = {&filter->resizeLeft, &filter->resizeRight, &filter->resizeTop, &filter->resizeBottom};
	for (i = 0; i < 4; i++) {
		if (filter->resizeExpressions[i].empty())
			continue;
		filter->compileExpression(filter->resizeExpressions[i]);
		if (filter->expressionCompiled())
			*resize[i] = filter->evaluateExpression<int>(0);
	}

	obs_source_t *target = obs_filter_get_target(filter->context);
	/* Determine offsets from expansion values. */
	int baseWidth  = filter->baseWidth;
	int baseHeight = filter->baseHeight;

	filter->total_width  = filter->resizeLeft + baseWidth + filter->resizeRight;
	filter->total_height = filter->resizeTop + baseHeight + filter->resizeBottom;

	filter->uvScale.x         = (float)filter->total_width / baseWidth;
	filter->uvScale.y         = (float)filter->total_height / baseHeight;
	filter->uvOffset.x        = (float)(-filter->resizeLeft) / baseWidth;
	filter->uvOffset.y        = (float)(-filter->resizeTop) / baseHeight;
	filter->uvPixelInterval.x = 1.0f / baseWidth;
	filter->uvPixelInterval.y = 1.0f / baseHeight;

	filter->uvScaleBinding  = filter->uvScale;
	filter->uvOffsetBinding = filter->uvOffset;
}

void ShaderFilter::videoRender(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	ShaderFilter *filter = static_cast<ShaderFilter *>(data);
	size_t        passes, i, j;

	if (filter->effect != nullptr) {
		obs_source_t *target, *parent, *source;
		gs_texture_t *texture;
		uint32_t      parent_flags;

		source = filter->context;
		target = obs_filter_get_target(filter->context);
		parent = obs_filter_get_parent(filter->context);

		if (!target) {
			blog(LOG_INFO, "filter '%s' being processed with no target!",
					obs_source_get_name(filter->context));
			return;
		}
		if (!parent) {
			blog(LOG_INFO, "filter '%s' being processed with no parent!",
					obs_source_get_name(filter->context));
			return;
		}

		size_t cx = filter->total_width;
		size_t cy = filter->total_height;

		if (!cx || !cy) {
			obs_source_skip_video_filter(filter->context);
			return;
		}

		for (i = 0; i < filter->paramList.size(); i++) {
			if (filter->paramList[i])
				filter->paramList[i]->videoRender(filter);
		}

		if (!filter->filter_texrender)
			filter->filter_texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);

		const char *id = obs_source_get_id(parent);
		parent_flags   = obs_get_source_output_flags(id);

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		if (gs_texrender_begin(filter->filter_texrender, cx, cy)) {
			bool        custom_draw = (parent_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
			bool        async       = (parent_flags & OBS_SOURCE_ASYNC) != 0;
			struct vec4 clear_color;

			vec4_zero(&clear_color);
			gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
			gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);

			if (target == parent && !custom_draw && !async)
				obs_source_default_render(target);
			else
				obs_source_video_render(target);

			gs_texrender_end(filter->filter_texrender);
		}

		gs_blend_state_pop();

		enum obs_allow_direct_render allow_bypass = OBS_NO_DIRECT_RENDERING;
		bool canBypass = (target == parent) && (allow_bypass == OBS_ALLOW_DIRECT_RENDERING) &&
				((parent_flags & OBS_SOURCE_CUSTOM_DRAW) == 0) &&
				((parent_flags & OBS_SOURCE_ASYNC) == 0);

		const char *tech_name = "Draw";

		if (canBypass) {
			gs_technique_t *tech = gs_effect_get_technique(filter->effect, tech_name);

			passes = gs_technique_begin(tech);
			for (i = 0; i < passes; i++) {
				gs_technique_begin_pass(tech, i);
				obs_source_video_render(target);
				gs_technique_end_pass(tech);
			}
			gs_technique_end(tech);
			for (j = 0; j < filter->paramList.size(); j++) {
				filter->paramList[j]->onTechniqueEnd(filter, tech_name, texture);
			}
		} else {
			texture = gs_texrender_get_texture(filter->filter_texrender);
			gs_eparam_t *image;
			if (texture) {
				gs_technique_t *tech = gs_effect_get_technique(filter->effect, tech_name);
				try {
					ShaderParameter *p = filter->paramMap.at("image");
					image              = p->getParameter()->getParam();
				} catch (std::out_of_range) {
					image = NULL;
				}

				gs_effect_set_texture(image, texture);

				passes = gs_technique_begin(tech);
				for (i = 0; i < passes; i++) {
					gs_technique_begin_pass(tech, i);
					gs_draw_sprite(texture, 0, cx, cy);
					gs_technique_end_pass(tech);
					/*Handle Buffers*/
					for (j = 0; j < filter->paramList.size(); j++) {
						filter->paramList[j]->onPass(filter, tech_name, j, texture);
					}
				}
				gs_technique_end(tech);
				for (j = 0; j < filter->paramList.size(); j++) {
					filter->paramList[j]->onTechniqueEnd(filter, tech_name, texture);
				}
			}
		}
	} else {
		obs_source_skip_video_filter(filter->context);
	}
}

void ShaderFilter::videoRenderSource(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	ShaderFilter *filter = static_cast<ShaderFilter *>(data);
	size_t        passes, i, j;

	obs_source_t *source;
	gs_texture_t *texture;
	uint32_t      parent_flags;

	source = filter->context;

	if (!source) {
		blog(LOG_INFO, "no source?");
		return;
	}

	size_t cx = obs_source_get_base_width(source);
	size_t cy = obs_source_get_base_height(source);

	if (!cx || !cy) {
		return;
	}

	if (filter->effect != nullptr) {
		for (i = 0; i < filter->paramList.size(); i++) {
			if (filter->paramList[i])
				filter->paramList[i]->videoRender(filter);
		}

		if (!filter->filter_texrender)
			filter->filter_texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);

		const char *id = obs_source_get_id(source);
		parent_flags   = obs_get_source_output_flags(id);

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		if (gs_texrender_begin(filter->filter_texrender, cx, cy)) {
			bool        custom_draw = (parent_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
			bool        async       = (parent_flags & OBS_SOURCE_ASYNC) != 0;
			struct vec4 clear_color;

			vec4_zero(&clear_color);
			gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
			gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);

			gs_texrender_end(filter->filter_texrender);
		}

		gs_blend_state_pop();

		const char *tech_name = "Draw";

		texture = gs_texrender_get_texture(filter->filter_texrender);
		gs_eparam_t *image;
		if (texture) {
			gs_technique_t *tech = gs_effect_get_technique(filter->effect, tech_name);
			try {
				ShaderParameter *p = filter->paramMap.at("image");
				image              = p->getParameter()->getParam();
			} catch (std::out_of_range) {
				image = NULL;
			}

			gs_effect_set_texture(image, texture);

			passes = gs_technique_begin(tech);
			for (i = 0; i < passes; i++) {
				gs_technique_begin_pass(tech, i);
				gs_draw_sprite(texture, 0, filter->total_width, filter->total_height);
				gs_technique_end_pass(tech);
				/*Handle Buffers*/
				for (j = 0; j < filter->paramList.size(); j++) {
					filter->paramList[j]->onPass(filter, tech_name, j, texture);
				}
			}
			gs_technique_end(tech);
			for (j = 0; j < filter->paramList.size(); j++) {
				filter->paramList[j]->onTechniqueEnd(filter, tech_name, texture);
			}
		}
	} else {
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		if (gs_texrender_begin(filter->filter_texrender, cx, cy)) {
			bool        custom_draw = (parent_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
			bool        async       = (parent_flags & OBS_SOURCE_ASYNC) != 0;
			struct vec4 clear_color;

			vec4_zero(&clear_color);
			gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
			gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);

			gs_texrender_end(filter->filter_texrender);
		}

		gs_blend_state_pop();
		texture = gs_texrender_get_texture(filter->filter_texrender);
		if (texture) {
			const char *tech_name = "Draw";

			obs_base_effect f;
			gs_effect_t *   effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
			gs_eparam_t *   image  = gs_effect_get_param_by_name(effect, "image");
			gs_technique_t *tech   = gs_effect_get_technique(filter->effect, tech_name);

			gs_effect_set_texture(image, texture);

			passes = gs_technique_begin(tech);
			for (i = 0; i < passes; i++) {
				gs_technique_begin_pass(tech, i);
				gs_draw_sprite(texture, 0, filter->total_width, filter->total_height);
				gs_technique_end_pass(tech);
				/*Handle Buffers*/
				for (j = 0; j < filter->paramList.size(); j++) {
					filter->paramList[j]->onPass(filter, tech_name, j, texture);
				}
			}
			gs_technique_end(tech);
			for (j = 0; j < filter->paramList.size(); j++) {
				filter->paramList[j]->onTechniqueEnd(filter, tech_name, texture);
			}
		}
	}
}

void ShaderFilter::update(void *data, obs_data_t *settings)
{
	UNUSED_PARAMETER(settings);
	ShaderFilter *filter = static_cast<ShaderFilter *>(data);
	if (filter->needsReloading()) {
		filter->reload();
		obs_source_update_properties(filter->context);
	}
	size_t i;
	for (i = 0; i < filter->paramList.size(); i++) {
		if (filter->paramList[i])
			filter->paramList[i]->update(filter);
	}
	filter->baseHeight = obs_data_get_int(settings, "size.height");
	filter->baseWidth  = obs_data_get_int(settings, "size.width");
}

obs_properties_t *ShaderFilter::getProperties(void *data)
{
	ShaderFilter *    filter = static_cast<ShaderFilter *>(data);
	size_t            i;
	obs_properties_t *props = obs_properties_create();
	obs_properties_set_param(props, filter, NULL);

	std::string shaderPath = obs_get_module_data_path(obs_current_module());
	shaderPath += "/shaders";

	obs_properties_add_button(
			props, "reload_effect", obs_module_text("Reload"), shader_filter_reload_effect_clicked);

	obs_property_t *file_name = obs_properties_add_path(
			props, "shader_file_name", obs_module_text("File"), OBS_PATH_FILE, NULL, shaderPath.c_str());

	obs_property_set_modified_callback(file_name, shader_filter_file_name_changed);

	for (i = 0; i < filter->paramList.size(); i++) {
		if (filter->paramList[i])
			filter->paramList[i]->getProperties(filter, props);
	}
	return props;
}

obs_properties_t *ShaderFilter::getPropertiesSource(void *data)
{
	ShaderFilter *    filter = static_cast<ShaderFilter *>(data);
	size_t            i;
	obs_properties_t *props = obs_properties_create();
	obs_properties_set_param(props, filter, NULL);

	std::string shaderPath = obs_get_module_data_path(obs_current_module());
	shaderPath += "/shaders";

	obs_properties_add_button(
			props, "reload_effect", obs_module_text("Reload"), shader_filter_reload_effect_clicked);

	obs_property_t *file_name = obs_properties_add_path(
			props, "shader_file_name", obs_module_text("File"), OBS_PATH_FILE, NULL, shaderPath.c_str());

	obs_property_set_modified_callback(file_name, shader_filter_file_name_changed);

	obs_properties_add_int(props, "size.width", obs_module_text("Width"), 0, 4096, 1);

	obs_properties_add_int(props, "size.height", obs_module_text("Height"), 0, 4096, 1);

	for (i = 0; i < filter->paramList.size(); i++) {
		if (filter->paramList[i])
			filter->paramList[i]->getProperties(filter, props);
	}
	return props;
}

uint32_t ShaderFilter::getWidth(void *data)
{
	ShaderFilter *filter = static_cast<ShaderFilter *>(data);
	return filter->getWidth();
}

uint32_t ShaderFilter::getHeight(void *data)
{
	ShaderFilter *filter = static_cast<ShaderFilter *>(data);
	return filter->getHeight();
}

void ShaderFilter::mouseClick(
		void *data, const struct obs_mouse_event *event, int32_t type, bool mouse_up, uint32_t click_count)
{
	ShaderFilter *filter = static_cast<ShaderFilter *>(data);
	filter->_mouseType   = type;
	filter->_mouseUp     = mouse_up;
	filter->_clickCount  = click_count;
	filter->_mouseX      = event->x;
	filter->_mouseY      = event->y;
	filter->_mouseClickX = event->x;
	filter->_mouseClickY = event->y;
}

void ShaderFilter::mouseMove(void *data, const struct obs_mouse_event *event, bool mouse_leave)
{
	ShaderFilter *filter = static_cast<ShaderFilter *>(data);
	filter->_mouseX      = event->x;
	filter->_mouseY      = event->y;
	filter->_clickCount  = 0;
	filter->_mouseLeave  = mouse_leave;
}

void ShaderFilter::mouseWheel(void *data, const struct obs_mouse_event *event, int x_delta, int y_delta)
{
	ShaderFilter *filter      = static_cast<ShaderFilter *>(data);
	filter->_mouseX           = event->x;
	filter->_mouseY           = event->y;
	filter->_mouseWheelDeltaX = x_delta;
	filter->_mouseWheelDeltaY = y_delta;
	filter->_mouseWheelX += x_delta;
	filter->_mouseWheelY += y_delta;
}

void ShaderFilter::focus(void *data, bool focus)
{
	ShaderFilter *filter = static_cast<ShaderFilter *>(data);
	filter->_focus       = focus ? 1.0 : 0.0;
}

void ShaderFilter::keyClick(void *data, const struct obs_key_event *event, bool key_up)
{
	ShaderFilter *filter        = static_cast<ShaderFilter *>(data);
	filter->_keyModifiers       = event->modifiers;
	filter->_nativeKeyModifiers = event->native_modifiers;
	if (event->text)
		filter->_key = (double)event->text[0];
	filter->_keyUp = key_up;
}

void ShaderFilter::getDefaults(obs_data_t *settings)
{
	UNUSED_PARAMETER(settings);
}

static bool shader_filter_reload_effect_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(property);
	UNUSED_PARAMETER(props);
	ShaderFilter *filter = static_cast<ShaderFilter *>(data);
	filter->prepReload();
	obs_source_update(filter->context, NULL);

	return true;
}

static bool shader_filter_file_name_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	ShaderFilter *filter = static_cast<ShaderFilter *>(obs_properties_get_param(props));
	std::string   path   = obs_data_get_string(settings, obs_property_name(p));

	if (filter->getPath() != path) {
		filter->prepReload();
		filter->setPath(path);
		obs_source_update(filter->context, NULL);
	}

	return true;
}

bool obs_module_load(void)
{
	struct obs_source_info shader_filter = {0};
	shader_filter.id                     = "obs_shader_filter";
	shader_filter.type                   = OBS_SOURCE_TYPE_FILTER;
	shader_filter.output_flags           = OBS_SOURCE_VIDEO;
	shader_filter.get_name               = ShaderFilter::getName;
	shader_filter.create                 = ShaderFilter::create;
	shader_filter.destroy                = ShaderFilter::destroy;
	shader_filter.update                 = ShaderFilter::update;
	shader_filter.video_tick             = ShaderFilter::videoTick;
	shader_filter.video_render           = ShaderFilter::videoRender;
	shader_filter.get_defaults           = ShaderFilter::getDefaults;
	shader_filter.get_width              = ShaderFilter::getWidth;
	shader_filter.get_height             = ShaderFilter::getHeight;
	shader_filter.get_properties         = ShaderFilter::getProperties;

	obs_register_source(&shader_filter);

	struct obs_source_info shader_source = {0};
	shader_source.id                     = "obs_shader_source";
	shader_source.type                   = OBS_SOURCE_TYPE_INPUT;
	shader_source.output_flags           = OBS_SOURCE_VIDEO | OBS_SOURCE_INTERACTION;
	shader_source.get_name               = ShaderFilter::getName;
	shader_source.create                 = ShaderFilter::create;
	shader_source.destroy                = ShaderFilter::destroy;
	shader_source.update                 = ShaderFilter::update;
	shader_source.video_tick             = ShaderFilter::videoTickSource;
	shader_source.video_render           = ShaderFilter::videoRenderSource;
	shader_source.get_defaults           = ShaderFilter::getDefaults;
	shader_source.get_width              = ShaderFilter::getWidth;
	shader_source.get_height             = ShaderFilter::getHeight;
	shader_source.get_properties         = ShaderFilter::getPropertiesSource;

	shader_source.mouse_click = ShaderFilter::mouseClick;
	shader_source.mouse_move  = ShaderFilter::mouseMove;
	shader_source.mouse_wheel = ShaderFilter::mouseWheel;
	shader_source.focus       = ShaderFilter::focus;
	shader_source.key_click   = ShaderFilter::keyClick;

	obs_register_source(&shader_source);

	struct obs_audio_info aoi;
	obs_get_audio_info(&aoi);
	sample_rate     = (double)aoi.samples_per_sec;
	output_channels = (double)get_audio_channels(aoi.speakers);

	return true;
}

void obs_module_unload(void)
{
}
