/*
Copyright (C) 2018 by pkv <pkv.stream@gmail.com>, andersama <anderson.john.alexander@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/* For full GPL v2 compatibility it is required to build libs with
 * our open source sdk instead of steinberg sdk , see our fork:
 * https://github.com/pkviet/portaudio , branch : openasio
 * If you build with original asio sdk, you are free to do so to the
 * extent that you do not distribute your binaries.
 */

#include <util/bmem.h>
#include <util/platform.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <vector>
#include <JuceHeader.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("win-asio", "en-US")

#define blog(level, msg, ...) blog(level, "asio-input: " msg, ##__VA_ARGS__)

static void fill_out_devices(obs_property_t *prop);

static juce::AudioIODeviceType *deviceTypeAsio = AudioIODeviceType::createAudioIODeviceType_ASIO();

class ASIOPlugin;
class AudioCB;

static bool asio_device_changed(void *vptr, obs_properties_t *props, obs_property_t *list, obs_data_t *settings);
static bool asio_layout_changed(obs_properties_t *props, obs_property_t *list, obs_data_t *settings);
static bool fill_out_channels_modified(obs_properties_t *props, obs_property_t *list, obs_data_t *settings);

static std::vector<AudioCB *> callbacks;

// returns the size in bytes of a sample from an obs audio_format
int bytedepth_format(audio_format format)
{
	return (int)get_audio_bytes_per_channel(format);
}

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

static std::vector<speaker_layout> known_layouts = {
		SPEAKERS_MONO,    /**< Channels: MONO */
		SPEAKERS_STEREO,  /**< Channels: FL, FR */
		SPEAKERS_2POINT1, /**< Channels: FL, FR, LFE */
		SPEAKERS_4POINT0, /**< Channels: FL, FR, FC, RC */
		SPEAKERS_4POINT1, /**< Channels: FL, FR, FC, LFE, RC */
		SPEAKERS_5POINT1, /**< Channels: FL, FR, FC, LFE, RL, RR */
		SPEAKERS_7POINT1, /**< Channels: FL, FR, FC, LFE, RL, RR, SL, SR */
};
static std::vector<std::string> known_layouts_str = {"Mono", "Stereo", "2.1", "4.0", "4.1", "5.1", "7.1"};

class AudioCB : public juce::AudioIODeviceCallback {
private:
	AudioIODevice *  _device = nullptr;
	char *           _name   = nullptr;
	std::atomic<int> _write_index;
	double           sample_rate;
	TimeSliceThread *_thread = nullptr;
	CriticalSection  _output_lock;

public:
	struct AudioBufferInfo {
		AudioBuffer<float> buffer;
		obs_source_audio   out;
	};

	int write_index()
	{
		return _write_index.load();
	}

private:
	std::vector<AudioBufferInfo> buffers;
	AudioBufferInfo              out_buffer;

public:
	class AudioListener : public TimeSliceClient {
	private:
		std::vector<short> _route;
		obs_source_audio   in;
		obs_source_t *     source;

		bool     active;
		int      read_index = 0;
		int      wait_time  = 4;
		AudioCB *callback;
		AudioCB *current_callback;

		size_t   silent_buffer_size = 0;
		uint8_t *silent_buffer      = nullptr;

		inline bool set_data(AudioBufferInfo *info, obs_source_audio &out, const std::vector<short> &route,
				int *sample_rate)
		{
			out.speakers        = in.speakers;
			out.samples_per_sec = info->out.samples_per_sec;
			out.format          = AUDIO_FORMAT_FLOAT_PLANAR;
			out.timestamp       = info->out.timestamp;
			out.frames          = info->buffer.getNumSamples();

			*sample_rate = out.samples_per_sec;
			// cache a silent buffer
			size_t buffer_size = (out.frames * sizeof(bytedepth_format(out.format)));
			if (silent_buffer_size < buffer_size) {
				if (silent_buffer)
					bfree(silent_buffer);
				silent_buffer      = (uint8_t *)bzalloc(buffer_size);
				silent_buffer_size = buffer_size;
			}

			int       ichs = info->buffer.getNumChannels();
			int       ochs = get_audio_channels(out.speakers);
			uint8_t **data = (uint8_t **)info->buffer.getArrayOfWritePointers();

			bool muted = true;
			for (int i = 0; i < ochs; i++) {
				if (route[i] >= 0 && route[i] < ichs) {
					out.data[i] = data[route[i]];
					muted       = false;
				} else {
					out.data[i] = silent_buffer;
				}
			}
			return !muted;
		}

	public:
		AudioListener(obs_source_t *source, AudioCB *cb) : source(source), callback(cb)
		{
			silent_buffer_size = 2 * AUDIO_OUTPUT_FRAMES * sizeof(float);
			silent_buffer      = (uint8_t *)bzalloc(silent_buffer_size);
			active             = true;
		}

		~AudioListener()
		{
			if (silent_buffer)
				bfree(silent_buffer);
			disconnect();
		}

		void disconnect()
		{
			active = false;
		}

		void reconnect()
		{
			active = true;
		}

		void setOutput(obs_source_audio o)
		{
			in.format          = o.format;
			in.samples_per_sec = o.samples_per_sec;
			in.speakers        = o.speakers;
		}

		void setCurrentCallback(AudioCB *cb)
		{
			current_callback = cb;
		}

		void setCallback(AudioCB *cb)
		{
			callback = cb;
		}

		void setReadIndex(int idx)
		{
			read_index = idx;
		}

		void setRoute(std::vector<short> route)
		{
			_route = route;
		}

		AudioCB *getCallback()
		{
			return callback;
		}

		int useTimeSlice()
		{
			if (!active || callback != current_callback)
				return -1;
			int write_index = callback->write_index();
			if (read_index == write_index)
				return wait_time;

			std::vector<short> route           = _route;
			int                sample_rate     = 0;
			int                max_sample_rate = 1;
			int                m               = callback->buffers.size();

			while (read_index != write_index) {
				obs_source_audio out;
				bool unmuted = set_data(&callback->buffers[read_index], out, route, &sample_rate);
				if (unmuted && out.speakers)
					obs_source_output_audio(source, &out);
				if (sample_rate > max_sample_rate)
					max_sample_rate = sample_rate;
				read_index = (read_index + 1) % m;
			}
			wait_time = ((1000 / 2) * AUDIO_OUTPUT_FRAMES) / max_sample_rate;
			return wait_time;
		}
	};

	AudioIODevice *getDevice()
	{
		return _device;
	}

	const char *getName()
	{
		return _name;
	}

	void setDevice(AudioIODevice *device, const char *name)
	{
		_device = device;
		if (_name)
			bfree(_name);
		_name = bstrdup(name);
	}

	AudioCB(AudioIODevice *device, const char *name)
	{
		_write_index.store(0);
		_device = device;
		_name   = bstrdup(name);
	}

	~AudioCB()
	{
		bfree(_name);
	}

	static inline uint64_t conv_frames_to_time(const size_t sample_rate, const size_t frames)
	{
		if (!sample_rate)
			return 0;

		return (uint64_t)frames * 1000000000ULL / (uint64_t)sample_rate;
	}

	static inline size_t conv_time_to_frames(const size_t sample_rate, const uint64_t duration)
	{
		return (size_t)(duration * (uint64_t)sample_rate / 1000000000ULL);
	}

	void audioDeviceIOCallback(const float **inputChannelData, int numInputChannels, float **outputChannelData,
			int numOutputChannels, int numSamples)
	{
		//Input
		uint64_t ts = os_gettime_ns();
		for (int i = 0; i < numInputChannels; i++)
			buffers[_write_index].buffer.copyFrom(i, 0, inputChannelData[i], numSamples);
		buffers[_write_index].out.timestamp       = ts;
		buffers[_write_index].out.frames          = numSamples;
		buffers[_write_index].out.samples_per_sec = (uint32_t)sample_rate;
		_write_index.store((_write_index.load() + 1) % buffers.size());

		//Output
		const ScopedLock lock_here(_output_lock);

		uint64_t nxt_ts = ts + conv_frames_to_time(sample_rate, numSamples);
		uint64_t diff   = ts - out_buffer.out.timestamp;
		uint64_t offset = conv_time_to_frames(sample_rate, diff);
		uint64_t remainder = out_buffer.buffer.getNumSamples() - offset;
		int      buf_width = out_buffer.buffer.getNumSamples();

		size_t end = std::min(offset + numSamples, (unsigned long long)out_buffer.buffer.getNumSamples());
		int    count = numSamples;
		for (int ch = 0; ch < numOutputChannels; ch++) {
			float *f = out_buffer.buffer.getWritePointer(ch);
			int          to = 0;
			//copy
			for (int i = offset; i < end; i++) {
				outputChannelData[ch][to] = f[i];
				to++;
			}
			for (int i = 0; to < numSamples; i++) {
				outputChannelData[ch][to] = f[i];
				to++;
			}
			to = 0;
			//shift
			for (int i = offset; i < buf_width; i++)
				f[to] = f[i];
			//zero out last
			for (; to < buf_width; to++)
				f[to] = 0.0f;
		}
		
		out_buffer.out.timestamp = nxt_ts;
	}

	//frames must not be null
	void write_out(struct audio_data *frames, std::vector<uint16_t> route, speaker_layout speakers)
	{
		float ** in    = out_buffer.buffer.getArrayOfWritePointers();
		float ** out    = (float**)frames->data;
		if (frames->timestamp >= out_buffer.out.timestamp) {
			const ScopedLock lock_here(_output_lock);
			uint64_t diff   = frames->timestamp - out_buffer.out.timestamp;
			uint64_t offset = conv_time_to_frames(sample_rate, diff);

			int channels = out_buffer.buffer.getNumChannels();
			for (int i = 0; i < route.size(); i++) {
				if (route[i] < channels)
					FloatVectorOperations::add(in[route[i]] + offset, out[i], frames->frames);
			}
		}
	}

	void add_client(AudioListener *client)
	{
		if (!_thread)
			_thread = new TimeSliceThread("");

		client->setCurrentCallback(this);
		client->setReadIndex(_write_index);
		_thread->addTimeSliceClient(client);
	}

	void remove_client(AudioListener *client)
	{
		if (_thread)
			_thread->removeTimeSliceClient(client);
	}

	void audioDeviceAboutToStart(juce::AudioIODevice *device)
	{
		blog(LOG_INFO, "Starting (%s)", device->getName().toStdString().c_str());
		juce::String name = device->getName();
		sample_rate       = device->getCurrentSampleRate();
		int buf_size      = device->getCurrentBufferSizeSamples();
		int target_size   = AUDIO_OUTPUT_FRAMES * 2;
		int count         = std::max(8, target_size / buf_size);
		int ch_count      = device->getActiveInputChannels().countNumberOfSetBits();
		int o_ch_count    = device->getActiveOutputChannels().countNumberOfSetBits();
		_write_index      = 0;

		if (buffers.size() < count)
			buffers.reserve(count);
		int i = 0;
		for (; i < buffers.size(); i++) {
			buffers[i].buffer              = AudioBuffer<float>(ch_count, buf_size);
			buffers[i].out.format          = AUDIO_FORMAT_FLOAT_PLANAR;
			buffers[i].out.samples_per_sec = sample_rate;
		}
		for (; i < count; i++) {
			AudioBufferInfo inf;
			inf.buffer              = AudioBuffer<float>(ch_count, buf_size);
			inf.out.format          = AUDIO_FORMAT_FLOAT_PLANAR;
			inf.out.samples_per_sec = sample_rate;
			buffers.push_back(inf);
		}

		// preallocate massive buffer
		out_buffer.buffer = AudioBuffer<float>(o_ch_count, buf_size * count);
		float **out = out_buffer.buffer.getArrayOfWritePointers();
		for (int ch = 0; ch < o_ch_count; ch++)
			FloatVectorOperations::fill(out[ch], 0.0f, out_buffer.buffer.getNumSamples());

		//out_buffer.buffer.clear();
		out_buffer.out.format          = AUDIO_FORMAT_FLOAT_PLANAR;
		out_buffer.out.samples_per_sec = sample_rate;
		out_buffer.out.timestamp = os_gettime_ns();

		if (!_thread) {
			_thread = new TimeSliceThread(name);
			_thread->startThread();
		} else {
			for (int i = 0; i < _thread->getNumClients(); i++) {
				AudioListener *l = static_cast<AudioListener *>(_thread->getClient(i));
				l->setCurrentCallback(this);
			}
			_thread->setCurrentThreadName(name);
			if (!_thread->isThreadRunning())
				_thread->startThread();
		}
	}

	void audioDeviceStopped()
	{
		blog(LOG_INFO, "Stopped (%s)", _device->getName().toStdString().c_str());
	}

	void audioDeviceError(const juce::String &errorMessage)
	{
		if (_thread)
			_thread->stopThread(200);
		std::string error = errorMessage.toStdString();
		blog(LOG_ERROR, "Device Error!\n%s", error.c_str());
	}
};

static bool show_panel(obs_properties_t *props, obs_property_t *property, void *data);

class ASIOPlugin {
private:
	AudioIODevice *         _device   = nullptr;
	AudioCB::AudioListener *_listener = nullptr;
	std::vector<uint16_t>   _route    = {};
	speaker_layout          _speakers = SPEAKERS_UNKNOWN;
	AudioCB *               _callback = nullptr;
	CriticalSection         _menu_lock;
	bool                    _is_input = false;

public:
	AudioIODevice *getDevice()
	{
		return _device;
	}

	ASIOPlugin::ASIOPlugin(obs_data_t *settings, obs_source_t *source)
		: _speakers(SPEAKERS_UNKNOWN), _is_input(true)
	{
		_listener = new AudioCB::AudioListener(source, nullptr);
	}

	ASIOPlugin::ASIOPlugin(obs_data_t *settings, obs_output_t *output)
		: _speakers(SPEAKERS_UNKNOWN), _is_input(false)
	{
	}

	ASIOPlugin::~ASIOPlugin()
	{
		AudioCB *cb = _listener->getCallback();
		_listener->disconnect();
		if (cb)
			cb->remove_client(_listener);
		delete _listener;
	}

	static void *Create(obs_data_t *settings, obs_source_t *source)
	{
		ASIOPlugin *plugin = new ASIOPlugin(settings, source);
		plugin->update(settings);
		return plugin;
	}

	static void *Create(obs_data_t *settings, obs_output_t *output)
	{
		ASIOPlugin *plugin = new ASIOPlugin(settings, output);
		plugin->update(settings);
		return plugin;
	}

	static bool Start(void *vptr)
	{
		return true;
	}

	static void Stop(void *vptr, uint64_t ts)
	{
	}

	static void RawAudio(void *vptr, struct audio_data *frames)
	{
		ASIOPlugin *plugin = static_cast<ASIOPlugin *>(vptr);
		plugin->raw_audio(frames);
	}

	static void RawAudio2(void *vptr, size_t mix_idx, struct audio_data *frames)
	{
		ASIOPlugin *plugin = static_cast<ASIOPlugin *>(vptr);
		plugin->raw_audio(frames);
	}

	void raw_audio(struct audio_data *frames)
	{
		/*merge w/ out buffer data*/
		if (_callback && frames) {
			const ScopedLock lock_here(_menu_lock);
			_callback->write_out(frames, _route, _speakers);
		}
	}

	static void Destroy(void *vptr)
	{
		ASIOPlugin *plugin = static_cast<ASIOPlugin *>(vptr);
		delete plugin;
		plugin = nullptr;
	}

	static obs_properties_t *Properties(void *vptr)
	{
		UNUSED_PARAMETER(vptr);
		obs_properties_t *            props;
		obs_property_t *              devices;
		obs_property_t *              format;
		obs_property_t *              panel;
		int                           max_channels = get_max_obs_channels();
		std::vector<obs_property_t *> route(max_channels, nullptr);

		props   = obs_properties_create();
		devices = obs_properties_add_list(props, "device_id", obs_module_text("Device"), OBS_COMBO_TYPE_LIST,
				OBS_COMBO_FORMAT_STRING);
		// obs_property_set_modified_callback(devices, asio_device_changed);
		obs_property_set_modified_callback2(devices, asio_device_changed, vptr);
		fill_out_devices(devices);
		obs_property_set_long_description(devices, obs_module_text("ASIO Devices"));

		format = obs_properties_add_list(props, "speaker_layout", obs_module_text("Format"),
				OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		for (size_t i = 0; i < known_layouts.size(); i++)
			obs_property_list_add_int(format, known_layouts_str[i].c_str(), known_layouts[i]);
		obs_property_set_modified_callback(format, asio_layout_changed);

		for (size_t i = 0; i < max_channels; i++) {
			route[i] = obs_properties_add_list(props, ("route " + std::to_string(i)).c_str(),
					obs_module_text(("Route." + std::to_string(i)).c_str()), OBS_COMBO_TYPE_LIST,
					OBS_COMBO_FORMAT_INT);
			obs_property_set_long_description(
					route[i], obs_module_text(("Route.Desc." + std::to_string(i)).c_str()));
		}

		panel = obs_properties_add_button2(props, "ctrl", obs_module_text("Control Panel"), show_panel, vptr);
		ASIOPlugin *   plugin = static_cast<ASIOPlugin *>(vptr);
		AudioIODevice *device = nullptr;
		if (plugin)
			device = plugin->getDevice();

		obs_property_set_visible(panel, device && device->hasControlPanel());

		return props;
	}

	void update(obs_data_t *settings)
	{
		std::string    name     = obs_data_get_string(settings, "device_id");
		speaker_layout layout   = (speaker_layout)obs_data_get_int(settings, "speaker_layout");
		AudioCB *      callback = nullptr;

		AudioIODevice *selected_device = nullptr;
		for (int i = 0; i < callbacks.size(); i++) {
			AudioCB *      cb     = callbacks[i];
			AudioIODevice *device = cb->getDevice();
			std::string    n      = cb->getName();
			if (n == name) {
				if (!device) {
					String deviceName = name.c_str();
					device            = deviceTypeAsio->createDevice(deviceName, deviceName);
					cb->setDevice(device, name.c_str());
				}
				selected_device = device;
				_device         = device;
				callback        = cb;
				break;
			}
		}

		auto bail = [this, &callback](bool is_input) {
			if (!is_input) {
				_callback = nullptr;
			} else {
				AudioCB *cb = _listener->getCallback();

				_listener->setCurrentCallback(callback);
				_listener->disconnect();

				if (cb)
					cb->remove_client(_listener);
			}
		};

		if (selected_device == nullptr) {
			bail(_is_input);
			return;
		}

		StringArray in_chs  = _device->getInputChannelNames();
		StringArray out_chs = _device->getOutputChannelNames();
		BigInteger  in      = 0;
		BigInteger  out     = 0;
		in.setRange(0, in_chs.size(), true);
		out.setRange(0, out_chs.size(), true);
		juce::String err;

		/* Open Up Particular Device */
		if (!_device->isOpen()) {
			err = _device->open(in, out, _device->getCurrentSampleRate(),
					_device->getCurrentBufferSizeSamples());
			if (!err.toStdString().empty()) {
				blog(LOG_WARNING, "%s", err.toStdString().c_str());
				bail(_is_input);
				return;
			}
		}

		if (!_is_input) {
			int                recorded_channels = get_audio_channels(layout);
			int                max_channels      = get_max_obs_channels();
			const ScopedLock   lock_here(_menu_lock);
			_route.clear();
			_route.reserve(max_channels);
			for (int i = 0; i < recorded_channels; i++) {
				std::string route_str = "route " + std::to_string(i);
				_route.push_back(obs_data_get_int(settings, route_str.c_str()));
			}
			for (int i = recorded_channels; i < max_channels; i++) {
				_route.push_back(-1);
			}
		} else {
			AudioCB *cb = _listener->getCallback();
			_listener->setCurrentCallback(callback);

			if (_device->isOpen() && !_device->isPlaying() && callback)
				_device->start(callback);

			if (callback) {
				if (cb != callback) {
					_listener->disconnect();
					if (cb)
						cb->remove_client(_listener);
				}

				int                recorded_channels = get_audio_channels(layout);
				int                max_channels      = get_max_obs_channels();
				std::vector<short> r;
				r.reserve(max_channels);

				for (int i = 0; i < recorded_channels; i++) {
					std::string route_str = "route " + std::to_string(i);
					r.push_back(obs_data_get_int(settings, route_str.c_str()));
				}
				for (int i = recorded_channels; i < max_channels; i++) {
					r.push_back(-1);
				}

				_listener->setRoute(r);

				obs_source_audio out;
				out.speakers = layout;
				_listener->setOutput(out);

				_listener->setCallback(callback);
				if (cb != callback) {
					_listener->reconnect();
					callback->add_client(_listener);
				}
			} else {
				_listener->disconnect();
				if (cb)
					cb->remove_client(_listener);
			}
		}
	}

	static void Update(void *vptr, obs_data_t *settings)
	{
		ASIOPlugin *plugin = static_cast<ASIOPlugin *>(vptr);
		if (plugin)
			plugin->update(settings);
	}

	static void Defaults(obs_data_t *settings)
	{
		struct obs_audio_info aoi;
		obs_get_audio_info(&aoi);
		int recorded_channels = get_audio_channels(aoi.speakers);
		int max_channels      = get_max_obs_channels();
		// default is muted channels
		for (int i = 0; i < recorded_channels; i++) {
			std::string name = "route " + std::to_string(i);
			obs_data_set_default_int(settings, name.c_str(), -1);
		}
		for (int i = recorded_channels; i < max_channels; i++) {
			std::string name = "route " + std::to_string(i);
			obs_data_set_default_int(settings, name.c_str(), -1);
		}

		obs_data_set_default_int(settings, "speaker_layout", aoi.speakers);
	}

	static const char *Name(void *unused)
	{
		UNUSED_PARAMETER(unused);
		return obs_module_text("Asio.Input");
	}

	static const char *Name_Output(void *unused)
	{
		UNUSED_PARAMETER(unused);
		return obs_module_text("Asio.Output");
	}
};

static bool show_panel(obs_properties_t *props, obs_property_t *property, void *data)
{
	if (!data)
		return false;
	ASIOPlugin *   plugin = static_cast<ASIOPlugin *>(data);
	AudioIODevice *device = plugin->getDevice();
	if (device && device->hasControlPanel())
		device->showControlPanel();
	return false;
}

static bool fill_out_channels_modified(obs_properties_t *props, obs_property_t *list, obs_data_t *settings)
{
	UNUSED_PARAMETER(props);
	std::string    name      = obs_data_get_string(settings, "device_id");
	AudioCB *      _callback = nullptr;
	AudioIODevice *_device   = nullptr;

	for (int i = 0; i < callbacks.size(); i++) {
		AudioCB *      cb     = callbacks[i];
		AudioIODevice *device = cb->getDevice();
		std::string    n      = cb->getName();
		if (n == name) {
			if (!device) {
				String deviceName = name.c_str();
				device            = deviceTypeAsio->createDevice(deviceName, deviceName);
				cb->setDevice(device, name.c_str());
			}
			_device   = device;
			_callback = cb;
			break;
		}
	}

	obs_property_list_clear(list);
	obs_property_list_add_int(list, obs_module_text("Mute"), -1);

	if (!_callback || !_device)
		return true;

	juce::StringArray in_names       = _device->getInputChannelNames();
	int               input_channels = in_names.size();

	int i = 0;

	for (; i < input_channels; i++)
		obs_property_list_add_int(list, in_names[i].toStdString().c_str(), i);

	return true;
}

static bool asio_device_changed(void *vptr, obs_properties_t *props, obs_property_t *list, obs_data_t *settings)
{
	size_t                        i;
	const char *                  curDeviceId  = obs_data_get_string(settings, "device_id");
	int                           max_channels = get_max_obs_channels();
	std::vector<obs_property_t *> route(max_channels, nullptr);
	speaker_layout                layout = (speaker_layout)obs_data_get_int(settings, "speaker_layout");
	obs_property_t *              panel  = obs_properties_get(props, "ctrl");

	int recorded_channels = get_audio_channels(layout);

	size_t itemCount = obs_property_list_item_count(list);
	bool   itemFound = false;

	for (i = 0; i < itemCount; i++) {
		const char *DeviceId = obs_property_list_item_string(list, i);
		if (strcmp(DeviceId, curDeviceId) == 0) {
			itemFound = true;
			break;
		}
	}

	if (!itemFound) {
		obs_property_list_insert_string(list, 0, " ", curDeviceId);
		obs_property_list_item_disable(list, 0, true);
	} else {
		for (i = 0; i < max_channels; i++) {
			std::string name = "route " + std::to_string(i);
			route[i]         = obs_properties_get(props, name.c_str());
			obs_property_list_clear(route[i]);
			obs_property_set_modified_callback(route[i], fill_out_channels_modified);
			obs_property_set_visible(route[i], i < recorded_channels);
		}
	}

	ASIOPlugin *plugin = static_cast<ASIOPlugin *>(vptr);
	if (plugin) {
		juce::AudioIODevice *device = plugin->getDevice();
		obs_property_set_visible(panel, device && device->hasControlPanel());
	}

	return true;
}

static bool asio_layout_changed(obs_properties_t *props, obs_property_t *list, obs_data_t *settings)
{
	int                           max_channels = get_max_obs_channels();
	std::vector<obs_property_t *> route(max_channels, nullptr);
	speaker_layout                layout            = (speaker_layout)obs_data_get_int(settings, "speaker_layout");
	int                           recorded_channels = get_audio_channels(layout);
	int                           i                 = 0;
	for (i = 0; i < max_channels; i++) {
		std::string name = "route " + std::to_string(i);
		route[i]         = obs_properties_get(props, name.c_str());
		obs_property_list_clear(route[i]);
		obs_property_set_modified_callback(route[i], fill_out_channels_modified);
		obs_property_set_visible(route[i], i < recorded_channels);
	}
	return true;
}

static void fill_out_devices(obs_property_t *prop)
{
	StringArray deviceNames(deviceTypeAsio->getDeviceNames());
	for (int j = 0; j < deviceNames.size(); j++) {
		bool found = false;
		for (int i = 0; i < callbacks.size(); i++) {
			AudioCB *      cb     = callbacks[i];
			AudioIODevice *device = cb->getDevice();
			std::string    n      = cb->getName();
			if (deviceNames[j].toStdString() == n) {
				found = true;
				break;
			}
		}
		if (!found) {
			char *   name = bstrdup(deviceNames[j].toStdString().c_str());
			AudioCB *cb   = new AudioCB(nullptr, name);
			bfree(name);
			callbacks.push_back(cb);
		}
	}

	obs_property_list_clear(prop);

	for (int i = 0; i < callbacks.size(); i++) {
		AudioCB *   cb = callbacks[i];
		const char *n  = cb->getName();
		obs_property_list_add_string(prop, n, n);
	}
}

bool obs_module_load(void)
{
	obs_audio_info aoi;
	obs_get_audio_info(&aoi);

	MessageManager::getInstance();

	deviceTypeAsio->scanForDevices();
	StringArray deviceNames(deviceTypeAsio->getDeviceNames());
	for (int j = 0; j < deviceNames.size(); j++) {
		char *name = bstrdup(deviceNames[j].toStdString().c_str());

		AudioCB *cb = new AudioCB(nullptr, name);
		bfree(name);
		callbacks.push_back(cb);
	}

	struct obs_output_info asio_output = {0};
	asio_output.id                     = "asio_output";
	asio_output.flags                  = OBS_OUTPUT_AUDIO | OBS_OUTPUT_MULTI_TRACK;
	asio_output.get_name               = ASIOPlugin::Name_Output;
	asio_output.create                 = ASIOPlugin::Create;
	asio_output.destroy                = ASIOPlugin::Destroy;
	asio_output.update                 = ASIOPlugin::Update;
	asio_output.get_defaults           = ASIOPlugin::Defaults;
	asio_output.get_properties         = ASIOPlugin::Properties;
	asio_output.raw_audio              = ASIOPlugin::RawAudio;
	asio_output.raw_audio2             = ASIOPlugin::RawAudio2;
	asio_output.start                  = ASIOPlugin::Start;
	asio_output.stop                   = ASIOPlugin::Stop;

	struct obs_source_info asio_input_capture = {0};
	asio_input_capture.id                     = "asio_input_capture";
	asio_input_capture.type                   = OBS_SOURCE_TYPE_INPUT;
	asio_input_capture.output_flags           = OBS_SOURCE_AUDIO;
	asio_input_capture.create                 = ASIOPlugin::Create;
	asio_input_capture.destroy                = ASIOPlugin::Destroy;
	asio_input_capture.update                 = ASIOPlugin::Update;
	asio_input_capture.get_defaults           = ASIOPlugin::Defaults;
	asio_input_capture.get_name               = ASIOPlugin::Name;
	asio_input_capture.get_properties         = ASIOPlugin::Properties;

	obs_register_source(&asio_input_capture);
	obs_register_output(&asio_output);
	return true;
}

void obs_module_unload(void)
{
	for (int i = 0; i < callbacks.size(); i++) {
		AudioCB *      cb     = callbacks[i];
		AudioIODevice *device = cb->getDevice();
		if (device) {
			if (device->isPlaying())
				device->stop();
			if (device->isOpen())
				device->close();
			delete device;
		}
		device = nullptr;
		delete cb;
	}

	delete deviceTypeAsio;
}
