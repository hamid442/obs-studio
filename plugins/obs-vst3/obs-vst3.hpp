#pragma once

#include <util/bmem.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <util/threading.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <vector>
#include <stdio.h>
#include <JuceHeader.h>
#include <juce_audio_processors/juce_audio_processors.h>

int get_max_obs_channels()
{
	static int channels = 0;
	if (channels > 0) {
		return channels;
	} else {
		for (int i = 0; i < 1024; i++) {
			int c = get_audio_channels((speaker_layout)i);
			if (c > channels)
				channels = c;
		}
		return channels;
	}
}

const int          obs_output_frames = AUDIO_OUTPUT_FRAMES;
const volatile int obs_max_channels  = get_max_obs_channels();

static VST3PluginFormat vst3format;
static VSTPluginFormat  vst2format;

static FileSearchPath search = vst3format.getDefaultLocationsToSearch();
StringArray           paths;

static FileSearchPath search_2x = vst2format.getDefaultLocationsToSearch();
StringArray           paths_2x;

StringArray    get_paths(VSTPluginFormat &f);
StringArray    get_paths(VST3PluginFormat &f);
FileSearchPath get_search_paths(VSTPluginFormat &f);
FileSearchPath get_search_paths(VST3PluginFormat &f);

void set_paths(VSTPluginFormat &f, StringArray p);
void set_paths(VST3PluginFormat &f, StringArray p);
void set_search_paths(VSTPluginFormat &f, FileSearchPath p);
void set_search_paths(VST3PluginFormat &f, FileSearchPath p);

class PluginWindow : public DialogWindow {
public:
	PluginWindow(const String &name, Colour backgroundColour, bool escapeKeyTriggersCloseButton,
			bool addToDesktop = true)
		: DialogWindow(name, backgroundColour, escapeKeyTriggersCloseButton, addToDesktop)
	{
		setUsingNativeTitleBar(true);
	}
	~PluginWindow()
	{
	}
	void closeButtonPressed()
	{
		setVisible(false);
	}
};

template<class PluginFormat> class PluginHost : private AudioProcessorListener {
private:
	juce::AudioBuffer<float> buffer;
	juce::MidiBuffer         midi;

	AudioPluginInstance * vst_instance     = nullptr;
	AudioPluginInstance * new_vst_instance = nullptr;
	AudioPluginInstance * old_vst_instance = nullptr;
	AudioProcessorEditor *editor           = nullptr;
	obs_source_t *        context          = nullptr;
	juce::MemoryBlock     vst_state;
	obs_data_t *          vst_settings = nullptr;
	juce::String          current_file = "";
	juce::String          current_name = "";

	PluginWindow *dialog = nullptr;

	juce::AudioProcessorParameter *param = nullptr;

	MidiMessageCollector midi_collector;
	MidiInput *          midi_input          = nullptr;
	juce::String         current_midi        = "";
	double               current_sample_rate = 0.0;

	PluginDescription desc;

	bool was_open = false;
	bool enabled  = true;
	bool swap     = false;

	void save_state(AudioProcessor *processor)
	{
		if (!vst_settings)
			vst_settings = obs_data_create();

		String state = "";
		if (processor) {
			processor->getStateInformation(vst_state);
			state = vst_state.toBase64Encoding();
		}
		obs_data_set_string(vst_settings, "state", state.toStdString().c_str());
	}

	void audioProcessorParameterChanged(AudioProcessor *processor, int parameterIndex, float newValue)
	{
		save_state(processor);

		std::string idx = std::to_string(parameterIndex);
		obs_data_set_double(vst_settings, idx.c_str(), newValue);
	}

	void audioProcessorChanged(AudioProcessor *processor)
	{
		save_state(processor);
	}

	void close_vst(AudioPluginInstance *inst)
	{
		if (inst) {
			inst->removeListener(this);
			AudioProcessorEditor *e = inst->getActiveEditor();
			if (e)
				delete e;
			inst->releaseResources();
			delete inst;
			inst = nullptr;
		}
	}

	void update(obs_data_t *settings)
	{
		static PluginFormat plugin_format;

		close_vst(old_vst_instance);
		old_vst_instance = nullptr;

		obs_audio_info aoi        = {0};
		bool           got_audio  = obs_get_audio_info(&aoi);
		juce::String   file       = obs_data_get_string(settings, "effect");
		juce::String   plugin     = obs_data_get_string(settings, "desc");
		juce::String   mididevice = obs_data_get_string(settings, "midi");

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
		bool         found = false;

		auto clear_vst = [this]() {
			close_vst(new_vst_instance);
			new_vst_instance = nullptr;
			desc             = PluginDescription();
			current_name     = "";
			swap             = true;
		};

		if (file.compare(current_file) != 0 || plugin.compare(current_name) != 0) {
			if (file.compare("") == 0 || plugin.compare("") == 0) {
				clear_vst();
				return;
			}

			was_open = host_open();

			juce::OwnedArray<juce::PluginDescription> descs;
			plugin_format.findAllTypesForFile(descs, file);
			if (descs.size() > 0) {
				blog(LOG_INFO, "%s", descs[0]->name.toStdString().c_str());
				// desc = *descs[0];
				if (got_audio) {
					String state    = obs_data_get_string(settings, "state");
					auto   callback = [state, this, &aoi, file](AudioPluginInstance *inst,
                                                                        const juce::String &           err) {
                                                new_vst_instance = inst;
                                                if (err.toStdString().length() > 0) {
                                                        blog(LOG_WARNING, "failed to load! %s",
                                                                        err.toStdString().c_str());
                                                }
                                                if (new_vst_instance) {
                                                        host_close();
                                                        new_vst_instance->setNonRealtime(false);
                                                        new_vst_instance->prepareToPlay((double)aoi.samples_per_sec,
                                                                        2 * obs_output_frames);

                                                        if (!vst_settings) {
                                                                juce::MemoryBlock m;
                                                                m.fromBase64Encoding(state);
                                                                new_vst_instance->setStateInformation(
                                                                                m.getData(), m.getSize());
                                                                vst_settings = obs_data_create();
                                                        } else {
                                                                obs_data_clear(vst_settings);
                                                        }

                                                        save_state(new_vst_instance);
                                                        new_vst_instance->addListener(this);
							current_name = new_vst_instance->getName();
						} else {
							current_name = "";
						}
                                                current_file = file;
                                                swap         = true;
					};

					for (int i = 0; i < descs.size(); i++) {
						if (plugin.compare(descs[i]->name) == 0) {
							desc  = *descs[i];
							found = true;
							break;
						}
					}
					if (found)
						plugin_format.createPluginInstanceAsync(desc,
								(double)aoi.samples_per_sec, 2 * obs_output_frames,
								callback);
					else
						clear_vst();
				}
			} else {
				clear_vst();
			}
		}
	}

	void save(obs_data_t *settings)
	{
		obs_data_set_string(settings, "state", obs_data_get_string(vst_settings, "state"));
	}

	void filter_audio(struct obs_audio_data *audio)
	{
		if (swap) {
			old_vst_instance = vst_instance;
			vst_instance     = new_vst_instance;
			new_vst_instance = nullptr;
			if (old_vst_instance)
				old_vst_instance->removeListener(this);
			if (was_open)
				host_clicked();
			swap = false;
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
	PluginFormat getFormat()
	{
		static PluginFormat plugin_format;
		return plugin_format;
	}

	PluginHost(obs_data_t *settings, obs_source_t *source) : context(source)
	{
		update(settings);
	}

	~PluginHost()
	{
		obs_data_release(vst_settings);
		host_close();
		close_vst(old_vst_instance);
		close_vst(vst_instance);
		close_vst(new_vst_instance);
	}

	void host_clicked()
	{
		if (has_gui()) {
			if (!dialog)
				dialog = new PluginWindow("", Colour(255, 255, 255), false, false);
			dialog->setName(vst_instance->getName());

			editor = vst_instance->createEditorIfNeeded();

			if (dialog) {
				if (editor)
					editor->setOpaque(true);
				dialog->setContentNonOwned(editor, true);
				if (!dialog->isOnDesktop()) {
					dialog->setOpaque(true);
					dialog->addToDesktop(ComponentPeer::StyleFlags::windowHasCloseButton |
									ComponentPeer::StyleFlags::windowHasTitleBar |
									ComponentPeer::StyleFlags::
											windowHasMinimiseButton,
							nullptr);

					dialog->setTopLeftPosition(40, 40);
				}
				dialog->setVisible(editor);
				if (editor)
					editor->setVisible(true);
			}
		}
	}

	void host_close()
	{
		if (dialog) {
			delete dialog;
			dialog = nullptr;
		}
	}

	bool has_gui()
	{
		return !swap && vst_instance && vst_instance->hasEditor();
	}

	bool host_open()
	{
		return dialog;
	}

	static bool vst_host_clicked(obs_properties_t *props, obs_property_t *property, void *vptr)
	{
		PluginHost *plugin = static_cast<PluginHost *>(vptr);
		plugin->host_clicked();
		return true;
	}

	static bool vst_selected_modified(
			void *vptr, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
	{
		static PluginFormat plugin_format;

		obs_property_t *desc_list = obs_properties_get(props, "desc");
		juce::String    file      = obs_data_get_string(settings, "effect");

		obs_property_list_clear(desc_list);

		juce::OwnedArray<juce::PluginDescription> descs;
		plugin_format.findAllTypesForFile(descs, file);
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

	static bool midi_selected_modified(
			void *vptr, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
	{
		obs_property_list_clear(property);
		juce::StringArray devices = MidiInput::getDevices();
		obs_property_list_add_string(property, "", "");
		for (int i = 0; i < devices.size(); i++)
			obs_property_list_add_string(property, devices[i].toRawUTF8(), devices[i].toRawUTF8());

		return true;
	}

	static obs_properties_t *Properties(void *vptr)
	{
		static PluginFormat plugin_format;

		PluginHost *plugin = static_cast<PluginHost *>(vptr);

		obs_properties_t *props;
		props = obs_properties_create();

		obs_property_t *vst_list;
		obs_property_t *desc_list;
		obs_property_t *midi_list;

		obs_property_t *vst_host_button;
		obs_property_t *bypass;
		vst_list = obs_properties_add_list(
				props, "effect", "vsts", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_set_modified_callback2(vst_list, vst_selected_modified, plugin);

		desc_list = obs_properties_add_list(
				props, "desc", obs_module_text("Plugin"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

		midi_list = obs_properties_add_list(
				props, "midi", obs_module_text("Midi"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_set_modified_callback2(midi_list, midi_selected_modified, nullptr);

		vst_host_button = obs_properties_add_button2(props, "vst_button", "Show", vst_host_clicked, plugin);

		/*Add VSTs to list*/
		bool scannable = plugin_format.canScanForPlugins();
		if (scannable) {
			juce::StringArray paths = get_paths(plugin_format);
			if (paths.size() < 1) {
				juce::FileSearchPath s = get_search_paths(plugin_format);
				paths                  = plugin_format.searchPathsForPlugins(s, true, true);
				set_paths(plugin_format, paths);
			}

			for (int i = 0; i < paths.size(); i++) {
				juce::String name = plugin_format.getNameOfPluginFromIdentifier(paths[i]);
				obs_property_list_add_string(
						vst_list, paths[i].toStdString().c_str(), name.toStdString().c_str());
			}
		}

		return props;
	}

	static void Update(void *vptr, obs_data_t *settings)
	{
		PluginHost *plugin = static_cast<PluginHost *>(vptr);
		if (plugin)
			plugin->update(settings);
	}

	static void Defaults(obs_data_t *settings)
	{
		/*Setup Defaults*/
		obs_data_set_default_string(settings, "effect", "None");
		obs_data_set_default_double(settings, "enable", true);
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
		return new PluginHost(settings, source);
	}

	static void Save(void *vptr, obs_data_t *settings)
	{
		PluginHost *plugin = static_cast<PluginHost *>(vptr);
		if (plugin)
			plugin->save(settings);
	}

	static void Destroy(void *vptr)
	{
		PluginHost *plugin = static_cast<PluginHost *>(vptr);
		delete plugin;
		plugin = nullptr;
	}

	static struct obs_audio_data *Filter_Audio(void *vptr, struct obs_audio_data *audio)
	{
		PluginHost *plugin = static_cast<PluginHost *>(vptr);
		plugin->filter_audio(audio);

		return audio;
	}
};
