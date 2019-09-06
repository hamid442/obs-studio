#include "osc-juce.hpp"

static OSCReceiver r;

//typename std::vector<std::pair<std::string, obs_weak_source_t*>> weak_list;
/*
std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems)
{
	std::string c = s;
	std::stringstream ss(c);
	std::string item;
	while (std::getline(ss, item, delim)) {
		elems.push_back(item);
	}
	return elems;
}
*/

namespace juce {
	const OSCType OSCTypes::int32 = 'i';
	const OSCType OSCTypes::float32 = 'f';
	const OSCType OSCTypes::string = 's';
	const OSCType OSCTypes::blob = 'b';
	const OSCType OSCTypes::colour = 'r';

	uint32 OSCColour::toInt32() const
	{
		return ByteOrder::makeInt(alpha, blue, green, red);
	}
};

typedef std::vector<std::pair<OSCAddress, obs_weak_source_t*>> weak_list;

static void get_filters(obs_source_t *parent, obs_source_t *child, void *vptr)
{
	std::string np = obs_source_get_name(parent);
	std::string p = obs_source_get_name(child);
	std::string addr = std::string("/") + np + std::string("/") + p;
	try {
		OSCAddress oscaddr(addr.c_str());
		obs_weak_source_t *weak = obs_source_get_weak_source(child);
		weak_list *list = static_cast<weak_list*>(vptr);
		list->push_back({ oscaddr, weak });
	} catch (...) {
		
	}
}

static bool get_sources(void *vptr, obs_source_t *source)
{
	std::string p = obs_source_get_name(source);
	std::string addr = std::string("/") + p;
	try {
		OSCAddress oscaddr(addr.c_str());
		obs_weak_source_t *weak = obs_source_get_weak_source(source);
		weak_list *list = static_cast<weak_list*>(vptr);
		list->push_back({ oscaddr, weak });
		obs_source_enum_filters(source, get_filters, list);
	} catch (...) {

	}
	return true;
}

weak_list OSCAddresses()
{
	weak_list addrs;
	obs_enum_sources(get_sources, &addrs);
	return addrs;
}
//OSCReceiver::RealtimeCallback
class ParameterListener : public OSCReceiver::Listener<OSCReceiver::RealtimeCallback> {
private:
	weak_list _addrs;
	bool needs_refresh = true;
public:
	ParameterListener()
	{
	};
	~ParameterListener()
	{
	};
	void refresh()
	{
		_addrs = OSCAddresses();
	}
	void oscUpdateSource(const OSCMessage &message, obs_source_t *source)
	{
		obs_data_t *settings = obs_source_get_settings(source);

		obs_source_type t = obs_source_get_type(source);
		uint32_t flags = obs_source_get_flags(source);
		obs_source_t *scene_source = obs_frontend_get_current_scene();
		obs_scene_t *scene = obs_scene_from_source(scene_source);
		const char *source_name = obs_source_get_name(source);
		obs_sceneitem_t *scene_item = obs_scene_find_source(scene, source_name);

		obs_transform_info transform_info = { 0 };
		obs_sceneitem_crop crop_info = { 0 };

		if (scene_item) {
			obs_sceneitem_get_info(scene_item, &transform_info);
			obs_sceneitem_get_crop(scene_item, &crop_info);
		}

		for (int i = 0; (i+1) < message.size();) {
			OSCArgument arg = message[i];
			OSCArgument arg2 = message[i + 1];
			if (arg.isString()) {
				juce::String argname = arg.getString();
				bool is_param = argname.startsWith("param.");
				if (is_param) {
					argname = argname.substring(6);
					//argname.replaceFirstOccurrenceOf("param.", "");
					if (arg2.isColour()) {
						vec4 color;
						OSCColour c = arg2.getColour();
						vec4_from_rgba(&color, c.toInt32());
						obs_data_set_vec4(settings, argname.toUTF8(), &color);
					} else if (arg2.isString()) {
						obs_data_set_string(settings, argname.toUTF8(), arg2.getString().toUTF8());
					} else if (arg2.isFloat32()) {
						obs_data_set_double(settings, argname.toUTF8(), arg2.getFloat32());
					} else if (arg2.isInt32()) {
						obs_data_set_int(settings, argname.toUTF8(), arg2.getInt32());
					} else if (arg2.isBlob()) {
						juce::String b = arg2.getBlob().toString();
						obs_data_set_string(settings, argname.toUTF8(), b.toUTF8());
					}
				} else {
					if (arg2.isColour()) {

					} else if (arg2.isString()) {
						juce::String value = arg2.getString();
						if (argname.compareIgnoreCase("rename") == 0)
							obs_source_set_name(source, value.toUTF8());
					} else if (arg2.isFloat32()) {
						float value = arg2.getFloat32();
						if (flags & OBS_SOURCE_AUDIO) {
							if (argname.compareIgnoreCase("volume") == 0)
								obs_source_set_volume(source, value);
							else if (argname.compareIgnoreCase("balance") == 0)
								obs_source_set_balance_value(source, value);
							else if (argname.compareIgnoreCase("muted") == 0)
								obs_source_set_muted(source, fabsf(value) > LARGE_EPSILON);
						}
						if (scene_item) {
							if (argname.compareIgnoreCase("rot") == 0)
								transform_info.rot = value;
							else if (argname.compareIgnoreCase("visible") == 0)
								obs_sceneitem_set_visible(scene_item, value);
							else if (argname.compareIgnoreCase("scale_x") == 0)
								transform_info.scale.x = value;
							else if (argname.compareIgnoreCase("scale_y") == 0)
								transform_info.scale.y = value;
							else if (argname.compareIgnoreCase("bounds_x") == 0)
								transform_info.bounds.x = value;
							else if (argname.compareIgnoreCase("bounds_y") == 0)
								transform_info.bounds.y = value;
							else if (argname.compareIgnoreCase("pos_x") == 0)
								transform_info.pos.x = value;
							else if (argname.compareIgnoreCase("pos_y") == 0)
								transform_info.pos.y = value;
							else if (argname.compareIgnoreCase("crop_left") == 0)
								crop_info.left = value;
							else if (argname.compareIgnoreCase("crop_top") == 0)
								crop_info.top = value;
							else if (argname.compareIgnoreCase("crop_right") == 0)
								crop_info.right = value;
							else if (argname.compareIgnoreCase("crop_bottom") == 0)
								crop_info.bottom = value;
							else if (argname.compareIgnoreCase("scene_order") == 0)
								obs_sceneitem_set_order_position(scene_item, value);
							else if (argname.compareIgnoreCase("scene_order_shift") == 0)
								obs_sceneitem_set_order(scene_item, (obs_order_movement)((int)value));
						}
						if (argname.compareIgnoreCase("enabled") == 0)
							obs_source_set_enabled(source, fabsf(value) > LARGE_EPSILON);

					} else if (arg2.isInt32()) {
						int value = arg2.getInt32();
						if (argname.compareIgnoreCase("muted") == 0)
							obs_source_set_muted(source, value != 0);
						else if (argname.compareIgnoreCase("enabled") == 0)
							obs_source_set_enabled(source, value != 0);
						if (scene_item) {
							if (argname.compareIgnoreCase("rot") == 0)
								transform_info.rot = value;
							else if (argname.compareIgnoreCase("alignment") == 0)
								transform_info.alignment = value;
							else if (argname.compareIgnoreCase("visible") == 0)
								obs_sceneitem_set_visible(scene_item, value);
							else if (argname.compareIgnoreCase("bounds_alignment") == 0)
								transform_info.bounds_alignment = value;
							else if (argname.compareIgnoreCase("scale_x") == 0)
								transform_info.scale.x = value;
							else if (argname.compareIgnoreCase("scale_y") == 0)
								transform_info.scale.y = value;
							else if (argname.compareIgnoreCase("bounds_x") == 0)
								transform_info.bounds.x = value;
							else if (argname.compareIgnoreCase("bounds_y") == 0)
								transform_info.bounds.y = value;
							else if (argname.compareIgnoreCase("pos_x") == 0)
								transform_info.pos.x = value;
							else if (argname.compareIgnoreCase("pos_y") == 0)
								transform_info.pos.y = value;
							else if (argname.compareIgnoreCase("crop_left") == 0)
								crop_info.left = value;
							else if (argname.compareIgnoreCase("crop_top") == 0)
								crop_info.top = value;
							else if (argname.compareIgnoreCase("crop_right") == 0)
								crop_info.right = value;
							else if (argname.compareIgnoreCase("crop_bottom") == 0)
								crop_info.bottom = value;
							else if (argname.compareIgnoreCase("scale_filter") == 0)
								obs_sceneitem_set_scale_filter(scene_item, (obs_scale_type)value);
							else if (argname.compareIgnoreCase("scene_order") == 0)
								obs_sceneitem_set_order_position(scene_item, value);
							else if (argname.compareIgnoreCase("scene_order_shift") == 0)
								obs_sceneitem_set_order(scene_item, (obs_order_movement)value);
						}
					} else if (arg2.isBlob()) {
						
					}
				}
				i += 2;
			} else {
				i += 1;
			}
		}

		if (scene_item) {
			obs_sceneitem_set_info(scene_item, &transform_info);
			obs_sceneitem_set_crop(scene_item, &crop_info);
		}

		obs_source_update(source, settings);

		obs_source_release(scene_source);
		obs_data_release(settings);
	}
	void oscMessageHandler(const OSCMessage &message, weak_list addrs)
	{
		OSCAddressPattern p = message.getAddressPattern();

		for (int i = 0; i < addrs.size(); i++) {
			if (p.matches(addrs[i].first)) {
				obs_source_t *source = obs_weak_source_get_source(addrs[i].second);
				if (source) {
					oscUpdateSource(message, source);
					obs_source_release(source);
				}
			}
		}
	}
	void oscBundleHandler(const OSCBundle &bundle, weak_list addrs)
	{
		for (int i = 0; i < bundle.size(); i++) {
			OSCBundle::Element e = bundle[i];
			if (e.isBundle()) {
				oscBundleHandler(e.getBundle(), addrs);
			} else if (e.isMessage()) {
				oscMessageHandler(e.getMessage(), addrs);
			}
		}
	}
	void oscMessageReceived(const OSCMessage &message)
	{
		oscMessageHandler(message, _addrs);
	}
	void oscBundleRecieved(const OSCBundle &bundle)
	{
		oscBundleHandler(bundle, _addrs);
	}
};

static ParameterListener *l = nullptr;
static obs_data_t *osc_settings = nullptr;
char *prog_dir = nullptr;

bool obs_module_load()
{
	MessageManager::getInstance();

	l = new ParameterListener();

	prog_dir = os_get_config_path_ptr("obs-studio\\plugin_config\\osc-juce");
	if (prog_dir)
		os_mkdirs(prog_dir);

	std::string path = prog_dir + std::string("\\settings.json");
	osc_settings = obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!osc_settings)
		osc_settings = obs_data_create();

	obs_data_set_default_int(osc_settings, "port", 0);
	int port = obs_data_get_int(osc_settings, "port");

	auto menu_callback = [](void *) {
		int lastport = obs_data_get_int(osc_settings, "port");
		lastport = QInputDialog::getInt(nullptr, obs_module_text("Port"), obs_module_text("Port"), lastport, 0, 65535, 1);
		r.disconnect();
		r.removeListener(l);
		obs_data_set_int(osc_settings, "port", lastport);
		bool success = r.connect(lastport);
		r.addListener(l);
		blog(LOG_INFO, "OSC Connect %s: port<%i>", success ? "Success" : "Failure", lastport);
	};

	bool success = r.connect(port);
	r.addListener(l);
	blog(LOG_INFO, "OSC Connect %s: port<%i>", success ? "Success" : "Failure", port);

	obs_frontend_add_tools_menu_item(obs_module_text("OSC Settings"), menu_callback, nullptr);

	return true;
}

static void source_handler(void*, calldata_t*)
{
	l->refresh();
}

static void frontend_handler(enum obs_frontend_event ev,
	void *private_data)
{
	switch (ev) {
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		obs_source_t *scene_source = obs_frontend_get_current_scene();
		signal_handler_t *h = obs_source_get_signal_handler(scene_source);
		if (h) {
			signal_handler_connect(h, "item_add", source_handler, nullptr);
			signal_handler_connect(h, "item_remove", source_handler, nullptr);
		}
		obs_source_release(scene_source);
		l->refresh();
	}
}

void obs_module_post_load()
{
	obs_frontend_add_event_callback(frontend_handler, nullptr);
}

void obs_module_unload()
{
	std::string path = prog_dir + std::string("\\settings.json");
	obs_data_save_json_safe(osc_settings, path.c_str(), "tmp", "bak");
	bfree(prog_dir);
	obs_data_release(osc_settings);
	r.disconnect();
	r.removeListener(l);
	obs_frontend_remove_event_callback(frontend_handler, nullptr);
	delete l;
}
