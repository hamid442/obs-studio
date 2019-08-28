#pragma once
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <JuceHeader.h>

static Image windowIcon = Image();

template<class PluginFormat> class VSTWindow;
template<class PluginFormat> class PluginHost;

template<class PluginFormat> class VSTWindow : public DialogWindow {
public:
	VSTWindow(const String &name, Colour backgroundColour, bool escapeKeyTriggersCloseButton,
			PluginHost<PluginFormat> *owner, bool addToDesktop = false)
		: DialogWindow(name, backgroundColour, escapeKeyTriggersCloseButton, addToDesktop)
	{
		setVisible(false);
		setOpaque(true);
		setUsingNativeTitleBar(true);
	}
	~VSTWindow()
	{
	}
	void closeButtonPressed()
	{
		setVisible(false);
	}
};

//#define SHAREDPTR

template<class PluginFormat>
class PluginHost : private AudioProcessorListener
#ifdef SHAREDPTR
	, public std::enable_shared_from_this<PluginHost<PluginFormat>>
#endif
{
private:
	juce::AudioBuffer<float> buffer;
	juce::MidiBuffer         midi;
	double                   current_sample_rate = 0.0;
	/*
	AudioPluginInstance *vst_instance     = nullptr;
	AudioPluginInstance *new_vst_instance = nullptr;
	AudioPluginInstance *old_vst_instance = nullptr;

	std::shared_ptr<AudioPluginInstance> vst_instance;
	std::shared_ptr<AudioPluginInstance> new_vst_instance;
	std::shared_ptr<AudioPluginInstance> old_vst_instance;
	*/
	std::unique_ptr<AudioPluginInstance> vst_instance;
	std::unique_ptr<AudioPluginInstance> new_vst_instance;
	std::unique_ptr<AudioPluginInstance> old_vst_instance;
	
	PluginDescription    desc;

	obs_source_t *    context = nullptr;
	juce::MemoryBlock vst_state;
	obs_data_t *      vst_settings = nullptr;
	juce::String      current_file = "";
	juce::String      current_name = "";
	juce::String      current_midi = "";

	MidiMessageCollector midi_collector;
	MidiInput *          midi_input = nullptr;

	VSTWindow<PluginFormat> *dialog = nullptr;

	juce::AudioProcessorParameter *param = nullptr;

	bool swap         = false;
	bool updating     = false;
	bool asynchronous = false;

	PluginFormat plugin_format;

	void save_state(AudioProcessor *processor)
	{
		if (!vst_settings)
			vst_settings = obs_data_create();
		if (processor) {
			processor->getStateInformation(vst_state);
			String state = vst_state.toBase64Encoding();
			obs_data_set_string(vst_settings, "state", state.toStdString().c_str());
		} else {
			obs_data_set_string(vst_settings, "state", "");
		}
	}

	void set_enabled(AudioProcessor *processor, bool value)
	{
		juce::AudioProcessorParameter *nparam = processor->getBypassParameter();
		if (nparam) {
			nparam->beginChangeGesture();
			nparam->setValueNotifyingHost(((float)!value) * 1.0f);
			nparam->endChangeGesture();
		}
	}

	void save_processor(AudioProcessor *processor)
	{
		String name = "";
		if (processor)
			name = processor->getName();

		obs_data_set_string(vst_settings, "vst_processor", name.toStdString().c_str());
	}

	void audioProcessorParameterChanged(AudioProcessor *processor, int parameterIndex, float newValue)
	{
		if (!vst_settings)
			vst_settings = obs_data_create();

		std::string idx = std::to_string(parameterIndex);
		obs_data_set_double(vst_settings, idx.c_str(), newValue);

		save_processor(processor);
	}

	void audioProcessorChanged(AudioProcessor *processor)
	{
		if (!vst_settings)
			vst_settings = obs_data_create();

		save_processor(processor);
	}
	/*
	void close_vst(AudioPluginInstance *inst)
	{
		if (inst) {
			inst->removeListener(this);
			AudioProcessorEditor *e = inst->getActiveEditor();
			if (e)
				delete e;
			inst->releaseResources();
			delete inst;
		}
	}
	*/
	void close_vst(std::unique_ptr<AudioPluginInstance> &inst)
	{
		if (inst) {
			inst->removeListener(this);
			AudioProcessorEditor *e = inst->getActiveEditor();
			if (e)
				delete e;
			inst->releaseResources();
			inst.reset();
		}
	}
	void change_vst(AudioPluginInstance *inst, juce::String err, juce::String state, juce::String file,
			juce::String vst_processor, std::vector<std::pair<int, float>> vstsaved)
	{
		updating         = true;
		//new_vst_instance = inst;
		new_vst_instance.reset(inst);
		if (err.toStdString().length() > 0) {
			blog(LOG_WARNING, "failed to load! %s", err.toStdString().c_str());
		}
		if (new_vst_instance) {
			double sample_rate = new_vst_instance->getSampleRate();
			new_vst_instance->setNonRealtime(false);
			new_vst_instance->prepareToPlay(sample_rate, 2 * obs_output_frames);
			String new_vst_processor = new_vst_instance->getName();

			/* On first load restore VST w/ two methods, a state restore and parameter replay */
			if (!vst_settings && new_vst_processor.compare(vst_processor) == 0) {
				juce::MemoryBlock m;
				m.fromBase64Encoding(state);
				new_vst_instance->setStateInformation(m.getData(), m.getSize());
				/* For plugins which dynamically modify their parameter list */
				new_vst_instance->refreshParameterList();
				int size = new_vst_instance->getParameters().size();
				for (int i = 0; i < vstsaved.size(); i++) {
					int   idx = vstsaved[i].first;
					float f   = vstsaved[i].second;
					if (idx < size) {
						AudioProcessorParameter *param = new_vst_instance->getParameters()[idx];
						param->beginChangeGesture();
						param->setValueNotifyingHost(f);
						param->endChangeGesture();
					}
				}

				vst_settings = obs_data_create();
			}

			new_vst_instance->addListener(this);
			current_name = new_vst_processor;
		} else {
			current_name = "";
		}

		/* Save the new vst's state */
		save_state(new_vst_instance.get());

		current_file = file;
		swap         = true;
		updating     = false;
	}

	void update(obs_data_t *settings)
	{
		if (swap || updating)
			return;
		updating = true;
#ifdef SHAREDPTR
		std::weak_ptr<PluginHost<PluginFormat>> tmp_self = shared_from_this(); // self; // weak_from_this();

		auto tmp_keep = tmp_self.lock();
		if (!tmp_keep) {
			blog(LOG_INFO, "?");
			return;
		}
#endif
		;
		//close_vst(old_vst_instance);
		//old_vst_instance = nullptr;

		obs_audio_info aoi;
		bool           got_audio = obs_get_audio_info(&aoi);

		juce::String file       = obs_data_get_string(settings, "effect");
		juce::String plugin     = obs_data_get_string(settings, "desc");
		juce::String mididevice = obs_data_get_string(settings, "midi");

		auto midi_stop = [this]() {
			if (midi_input) {
				midi_input->stop();
				delete midi_input;
				midi_input = nullptr;
			}
		};

		double sps = (double)aoi.samples_per_sec;
		if (got_audio && current_sample_rate != sps) {
			midi_collector.reset(sps);
			current_sample_rate = sps;
		}

		if (mididevice.compare("") == 0) {
			midi_stop();
		} else if (mididevice.compare(current_midi) != 0) {
			midi_stop();

			juce::StringArray devices     = MidiInput::getDevices();
			int               deviceindex = 0;
			for (; deviceindex < devices.size(); deviceindex++) {
				if (devices[deviceindex].compare(mididevice) == 0)
					break;
			}
			MidiInput *nextdevice = MidiInput::openDevice(deviceindex, &midi_collector);
			// if we haven't reset, make absolute certain we have
			if (current_sample_rate == 0.0) {
				midi_collector.reset(48000.0);
				current_sample_rate = 48000.0;
			}
			midi_input = nextdevice;
			if (midi_input)
				midi_input->start();
		}

		juce::String err;
		auto         clear_vst = [this]() {
                        close_vst(new_vst_instance);
                        new_vst_instance = nullptr;
                        desc             = PluginDescription();
                        swap             = true;
                        updating         = false;
		};

		if (file.compare("") == 0 || plugin.compare("") == 0) {
			gui_close();
			clear_vst();
		} else if (file.compare(current_file) != 0 || plugin.compare(current_name)) {
			gui_close();
			juce::OwnedArray<juce::PluginDescription> descs;
			plugin_format.findAllTypesForFile(descs, file);
			if (descs.size() > 0) {
				if (true) {
					String state         = obs_data_get_string(settings, "state");
					String vst_processor = obs_data_get_string(settings, "vst_processor");

					std::vector<std::pair<int, float>> vst_saved;
					vst_saved.reserve(64);

					/* Copy edited parameters into a vector */
					obs_data_item_t *item = NULL;
					for (item = obs_data_first(settings); item; obs_data_item_next(&item)) {
						enum obs_data_type type  = obs_data_item_gettype(item);
						std::string        iname = obs_data_item_get_name(item);

						if (!obs_data_item_has_user_value(item))
							continue;
						if (type == OBS_DATA_NUMBER) {
							enum obs_data_number_type ntype = obs_data_item_numtype(item);
							try {
								int   idx = std::stoi(iname);
								float f   = 0;
								if (ntype == OBS_DATA_NUM_DOUBLE) {
									f = obs_data_item_get_double(item);
									vst_saved.push_back({idx, f});
								} else {
									f = (float)obs_data_item_get_int(item);
									vst_saved.push_back({idx, f});
								}
							} catch (...) {
							}
						}
					}
#ifdef SHAREDPTR
					auto callback = [state, tmp_self, file, vst_processor, vst_saved](
									AudioPluginInstance *inst,
									const juce::String & err) {
						auto myself = tmp_self.lock();
						if (myself)
							myself->change_vst(inst, err, state, file, vst_processor,
									vst_saved);
					};
#else
					auto callback = [=](AudioPluginInstance *inst, const juce::String &err) {
						this->change_vst(inst, err, state, file, vst_processor, vst_saved);
					};
#endif
					bool found = false;
					for (int i = 0; i < descs.size(); i++) {
						if (plugin.compare(descs[i]->name) == 0) {
							desc  = *descs[i];
							found = true;
							break;
						}
					}

					if (found) {
						if (asynchronous) {
							plugin_format.createPluginInstanceAsync(desc,
									(double)aoi.samples_per_sec,
									2 * obs_output_frames, callback);
						} else {
							String               err;
							AudioPluginInstance *inst =
									plugin_format.createInstanceFromDescription(
											desc,
											(double)aoi.samples_per_sec,
											2 * obs_output_frames, err);
							callback(inst, err);
						}
					} else {
						clear_vst();
					}
				}
			} else {
				clear_vst();
			}
		}
	}

	void save(obs_data_t *settings)
	{
		if (vst_instance)
			save_state(vst_instance.get());
		if (settings && vst_settings)
			obs_data_apply(settings, vst_settings);
	}

	void filter_audio(struct obs_audio_data *audio)
	{
		if (swap) {
			old_vst_instance.swap(vst_instance);
			vst_instance.swap(new_vst_instance);
			new_vst_instance.reset();
			//old_vst_instance.swap(vst_instance);
			//new_vst_instance.reset();
			/*
			old_vst_instance = vst_instance;
			vst_instance     = new_vst_instance;
			new_vst_instance = nullptr;
			swap             = false;
			*/
		}

		/*Process w/ VST*/
		if (vst_instance) {
			int chs = 0;
			for (; chs < obs_max_channels && audio->data[chs]; chs++)
				;

			struct obs_audio_info aoi;
			bool                  audio_info = obs_get_audio_info(&aoi);
			double                sps        = (double)aoi.samples_per_sec;

			if (audio_info) {
				vst_instance->prepareToPlay(sps, audio->frames);
				if (current_sample_rate != sps)
					midi_collector.reset(sps);
				current_sample_rate = sps;
			}

			midi_collector.removeNextBlockOfMessages(midi, audio->frames);
			buffer.setDataToReferTo((float **)audio->data, chs, audio->frames);
			param = vst_instance->getBypassParameter();

			if (param && param->getValue() != 0.0f)
				vst_instance->processBlockBypassed(buffer, midi);
			else
				vst_instance->processBlock(buffer, midi);

			midi.clear();
		}
	}

public:
	std::shared_ptr<PluginHost<PluginFormat>> get()
	{
		return shared_from_this();
	}

	PluginHost<PluginFormat>(obs_data_t *settings = nullptr, obs_source_t *source = nullptr) : context(source)
	{
	}

	~PluginHost<PluginFormat>()
	{
		if (midi_input) {
			midi_input->stop();
			delete midi_input;
			midi_input = nullptr;
		}
		if (dialog)
			delete dialog;

		obs_data_release(vst_settings);
		/*
		close_vst(old_vst_instance);
		close_vst(new_vst_instance);
		close_vst(vst_instance);
		*/
	}

	bool old_gui()
	{
		if (vst_instance && dialog)
			return current_name.compare(dialog->getName()) != 0;
		else
			return current_name.compare("") != 0;
	}

	void gui_clicked()
	{
		if (has_gui()) {
			if (!gui_open()) {
				dialog = new VSTWindow<PluginFormat>(
						desc.name, juce::Colour(255, 255, 255), false, this);
			}
			if (dialog) {
				AudioProcessorEditor *e = vst_instance->getActiveEditor();
				if (e) {
					e->setOpaque(true);
					e->setVisible(true);
					dialog->setContentNonOwned(e, true);
					gui_show();
				} else {
					e = vst_instance->createEditor();
					if (e) {
						e->setOpaque(true);
						e->setVisible(true);
						dialog->setContentNonOwned(e, true);
						gui_show();
					}
				}
			}
		}
	}

	void gui_show()
	{
		auto d = dialog;
		if (d) {
			// void *h = obs_frontend_get_main_window_handle();
			d->setOpaque(true);
			d->setName(current_name);
			if (!d->isOnDesktop()) {
				int f = d->getDesktopWindowStyleFlags();
				f |= (ComponentPeer::StyleFlags::windowHasCloseButton |
						ComponentPeer::StyleFlags::windowHasTitleBar |
						ComponentPeer::StyleFlags::windowIsResizable);
				d->addToDesktop(f);
				d->setTopLeftPosition(40, 40);
			}
			if (!d->isVisible())
				d->setVisible(true);
		}
	}

	void gui_close()
	{
		if (dialog)
			delete dialog;
		dialog = nullptr;
	}

	void gui_window_close()
	{
		dialog = nullptr;
	}

	bool has_gui()
	{
		return vst_instance && vst_instance->hasEditor();
	}

	bool gui_open()
	{
		return dialog;
	}

	static bool vst_gui_clicked(obs_properties_t *props, obs_property_t *property, void *vptr)
	{
#ifdef SHAREDPTR
		std::shared_ptr<PluginHost<PluginFormat>> *plugin =
				static_cast<std::shared_ptr<PluginHost<PluginFormat>> *>(vptr);
		if (plugin) {
			auto tmp = *plugin;
			if (tmp) {
				if (tmp->old_gui())
					tmp->gui_close();
				tmp->gui_clicked();
			}
		}
#else
		PluginHost<PluginFormat> *plugin = static_cast<PluginHost<PluginFormat> *>(vptr);
		if (plugin) {
			if (plugin->old_gui()) {
				plugin->gui_close();
			}
			plugin->gui_clicked();
		}
#endif
		return false;
	}

	static bool midi_selected_modified(
			void *vptr, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
	{
		obs_property_list_clear(property);
		juce::StringArray devices = MidiInput::getDevices();
		obs_property_list_add_string(property, "", "");
		for (int i = 0; i < devices.size(); i++)
			obs_property_list_add_string(property, devices[i].toRawUTF8(), devices[i].toRawUTF8());

		return false;
	}

	static bool vst_selected_modified(
			void *vptr, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
	{
		static PluginFormat f;

		obs_property_t *vst_host_button = obs_properties_get(props, "vst_button");
		obs_property_t *desc_list       = obs_properties_get(props, "desc");
		juce::String    file            = obs_data_get_string(settings, "effect");

		obs_property_list_clear(desc_list);

		juce::OwnedArray<juce::PluginDescription> descs;
		f.findAllTypesForFile(descs, file);
		bool has_options = descs.size() > 1;
		if (has_options)
			obs_property_list_add_string(desc_list, "", "");

		for (int i = 0; i < descs.size(); i++) {
			std::string n = descs[i]->name.toStdString();
			obs_property_list_add_string(desc_list, n.c_str(), n.c_str());
		}

		obs_property_set_enabled(desc_list, has_options);
		return true;
	}

	static obs_properties_t *Properties(void *vptr)
	{
		static PluginFormat f;
#ifdef SHAREDPTR
		std::shared_ptr<PluginHost<PluginFormat>> *plugin =
				static_cast<std::shared_ptr<PluginHost<PluginFormat>> *>(vptr);
#else
		PluginHost<PluginFormat> *plugin = static_cast<PluginHost<PluginFormat> *>(vptr);
#endif
		obs_properties_t *props;
		props = obs_properties_create();

		obs_property_t *vst_list;
		obs_property_t *desc_list;
		obs_property_t *midi_list;

		obs_property_t *vst_gui_button;
		obs_property_t *bypass;
		vst_list = obs_properties_add_list(
				props, "effect", obs_module_text("File"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		desc_list = obs_properties_add_list(
				props, "desc", obs_module_text("Plugin"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_set_modified_callback2(vst_list, vst_selected_modified, nullptr);

		midi_list = obs_properties_add_list(
				props, "midi", obs_module_text("Midi"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_set_modified_callback2(midi_list, midi_selected_modified, nullptr);

		vst_gui_button = obs_properties_add_button2(
				props, "vst_button", obs_module_text("Show"), vst_gui_clicked, plugin);

		obs_property_list_add_string(vst_list, "", "");
		/*Add VSTs to list*/
		bool scannable = f.canScanForPlugins();
		if (scannable) {
			StringArray p = get_paths(f);
			if (p.size() < 1) {
				p = f.searchPathsForPlugins(search, true, true);
				set_paths(f, p);
			}
			for (int i = 0; i < p.size(); i++) {
				juce::String name = f.getNameOfPluginFromIdentifier(p[i]);
				obs_property_list_add_string(
						vst_list, p[i].toStdString().c_str(), name.toStdString().c_str());
			}
		}

		return props;
	}

	static void Update(void *vptr, obs_data_t *settings)
	{
#ifdef SHAREDPTR
		std::shared_ptr<PluginHost<PluginFormat>> *plugin =
				static_cast<std::shared_ptr<PluginHost<PluginFormat>> *>(vptr);
		if (plugin) {
			auto tmp = *plugin;

			if (tmp)
				tmp->update(settings);
		}
#else
		PluginHost<PluginFormat> *plugin = static_cast<PluginHost<PluginFormat> *>(vptr);
		if (plugin)
			plugin->update(settings);
#endif
	}

	static void Defaults(obs_data_t *settings)
	{
		/*Setup Defaults*/
		obs_data_set_default_string(settings, "effect", "");
		obs_data_set_default_string(settings, "desc", "");
		obs_data_set_default_string(settings, "midi", "");
	}

	static const char *Name(void *unused)
	{
		UNUSED_PARAMETER(unused);
		static PluginFormat f;
		static std::string  type_name = std::string("VSTPlugin.") + f.getName().toStdString();
		return obs_module_text(type_name.c_str());
	}

	static void *Create(obs_data_t *settings, obs_source_t *source)
	{
#ifdef SHAREDPTR
		std::shared_ptr<PluginHost<PluginFormat>> *ptr = new std::shared_ptr<PluginHost<PluginFormat>>;
		*ptr = std::make_shared<PluginHost<PluginFormat>>(settings, source);
		ptr->get()->update(settings);
#else
		PluginHost<PluginFormat> *ptr = new PluginHost<PluginFormat>(settings, source);
		if (ptr)
			ptr->update(settings);
#endif
		return ptr;
	}

	static void Save(void *vptr, obs_data_t *settings)
	{
#ifdef SHAREDPTR
		std::shared_ptr<PluginHost<PluginFormat>> *plugin =
				static_cast<std::shared_ptr<PluginHost<PluginFormat>> *>(vptr);
		if (plugin) {
			auto tmp = *plugin;
			if (tmp)
				tmp->save(settings);
		}
#else
		PluginHost<PluginFormat> *plugin = static_cast<PluginHost<PluginFormat> *>(vptr);
		if (plugin)
			plugin->save(settings);
#endif
	}

	static void Destroy(void *vptr)
	{
#ifdef SHAREDPTR
		std::shared_ptr<PluginHost<PluginFormat>> *plugin =
				static_cast<std::shared_ptr<PluginHost<PluginFormat>> *>(vptr);
		if (plugin)
			plugin->reset();
		delete plugin;
		plugin = nullptr;
#else
		PluginHost<PluginFormat> *plugin = static_cast<PluginHost<PluginFormat> *>(vptr);
		if (plugin)
			delete plugin;
#endif
	}

	static struct obs_audio_data *Filter_Audio(void *vptr, struct obs_audio_data *audio)
	{
#ifdef SHAREDPTR
		std::shared_ptr<PluginHost<PluginFormat>> *plugin =
				static_cast<std::shared_ptr<PluginHost<PluginFormat>> *>(vptr);

		plugin->get()->filter_audio(audio);
#else
		PluginHost<PluginFormat> *plugin = static_cast<PluginHost<PluginFormat> *>(vptr);
		if (plugin)
			plugin->filter_audio(audio);
#endif
		return audio;
	}
};
