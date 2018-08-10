/*
Copyright (C) 2017 by pkv <pkv.stream@gmail.com>, andersama <anderson.john.alexander@gmail.com>

Based on Pulse Input plugin by Leonhard Oelke.

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
#pragma once

#include <util/bmem.h>
#include <util/platform.h>
#include <util/threading.h>
#include <util/circlebuf.h>
#include <obs-module.h>
#include <vector>
#include <list>
#include <unordered_map>
#include <stdio.h>
#include <string>
#include <sstream>
#include <windows.h>
#include <util/windows/WinHandle.hpp>
#include <bassasio.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("win-asio", "en-US")

#define blog(level, msg, ...) blog(level, "win-asio: " msg, ##__VA_ARGS__)

#define NSEC_PER_SEC  1000000000LL

#define TEXT_BUFFER_SIZE                obs_module_text("BufferSize")
#define TEXT_BUFFER_64_SAMPLES          obs_module_text("64_samples")
#define TEXT_BUFFER_128_SAMPLES         obs_module_text("128_samples")
#define TEXT_BUFFER_256_SAMPLES         obs_module_text("256_samples")
#define TEXT_BUFFER_512_SAMPLES         obs_module_text("512_samples")
#define TEXT_BUFFER_1024_SAMPLES        obs_module_text("1024_samples")
#define TEXT_BITDEPTH                   obs_module_text("BitDepth")

/* ======================================================================= */
/* conversion between BASS_ASIO and obs */

enum audio_format asio_to_obs_audio_format(DWORD format)
{
	switch (format) {
	case BASS_ASIO_FORMAT_16BIT:   return AUDIO_FORMAT_16BIT;
	case BASS_ASIO_FORMAT_32BIT:   return AUDIO_FORMAT_32BIT;
	case BASS_ASIO_FORMAT_FLOAT:   return AUDIO_FORMAT_FLOAT;
	default:                       break;
	}

	return AUDIO_FORMAT_UNKNOWN;
}

int bytedepth_format(audio_format format)
{
	return (int)get_audio_bytes_per_channel(format);
}

int bytedepth_format(DWORD format) {
	return bytedepth_format(asio_to_obs_audio_format(format));
}

DWORD obs_to_asio_audio_format(audio_format format)
{
	switch (format) {

	case AUDIO_FORMAT_16BIT:
		return BASS_ASIO_FORMAT_16BIT;
		// obs doesn't have 24 bit
	case AUDIO_FORMAT_32BIT:
		return BASS_ASIO_FORMAT_32BIT;

	case AUDIO_FORMAT_FLOAT:
	default:
		return BASS_ASIO_FORMAT_FLOAT;
	}
	// default to 32 float samples for best quality

}

enum speaker_layout asio_channels_to_obs_speakers(unsigned int channels)
{
	switch (channels) {
	case 1:   return SPEAKERS_MONO;
	case 2:   return SPEAKERS_STEREO;
	case 3:   return SPEAKERS_2POINT1;
	case 4:   return SPEAKERS_4POINT0;
	case 5:   return SPEAKERS_4POINT1;
	case 6:   return SPEAKERS_5POINT1;
		/* no layout for 7 channels */
	case 8:   return SPEAKERS_7POINT1;
	}
	return SPEAKERS_UNKNOWN;
}

/* ======================================================================= */
/* asio structs and classes */

struct asio_source_audio {
	uint8_t       *data[MAX_AUDIO_CHANNELS];
	uint32_t            frames;

	//enum speaker_layout speakers;
	volatile long		speakers;
	enum audio_format   format;
	uint32_t            samples_per_sec;

	uint64_t            timestamp;
};

audio_format get_planar_format(audio_format format) {
	switch (format) {
	case AUDIO_FORMAT_U8BIT:
		return AUDIO_FORMAT_U8BIT_PLANAR;

	case AUDIO_FORMAT_16BIT:
		return AUDIO_FORMAT_16BIT_PLANAR;

	case AUDIO_FORMAT_32BIT:
		return AUDIO_FORMAT_32BIT_PLANAR;

	case AUDIO_FORMAT_FLOAT:
		return AUDIO_FORMAT_FLOAT_PLANAR;
	}

	return format;
}

audio_format get_interleaved_format(audio_format format) {
	switch (format) {
	case AUDIO_FORMAT_U8BIT_PLANAR:
		return AUDIO_FORMAT_U8BIT;

	case AUDIO_FORMAT_16BIT_PLANAR:
		return AUDIO_FORMAT_16BIT;

	case AUDIO_FORMAT_32BIT_PLANAR:
		return AUDIO_FORMAT_32BIT;

	case AUDIO_FORMAT_FLOAT_PLANAR:
		return AUDIO_FORMAT_FLOAT;
	}

	return format;
}

int bytedepth_format(audio_format format);

#define CAPTURE_INTERVAL INFINITE
struct device_source_audio {
	uint8_t				**data;
	uint32_t			frames;
	long				input_chs;
	enum audio_format	format;
	uint32_t			samples_per_sec;
	uint64_t			timestamp;
};

class device_data;
class asio_data;

struct listener_pair {
	asio_data *asio_listener;
	device_data *device;
};

class asio_data {
private:
	uint8_t* silent_buffer = NULL;
	size_t silent_buffer_size = 0;
public:
	CRITICAL_SECTION settings_mutex;

	obs_source_t *source;

	/*asio device and info */
	const char *device;
	uint8_t device_index;

	uint64_t first_ts; //first timestamp
	/* channels info */
	DWORD input_channels; //total number of input channels
	DWORD output_channels; // number of output channels of device (not used)
	DWORD recorded_channels; // number of channels passed from device (including muted) to OBS; is at most 8
	long route[MAX_AUDIO_CHANNELS]; // stores the channel re-ordering info

	std::vector<short> unmuted_chs;
	std::vector<short> muted_chs;
	std::vector<long> required_signals;

	//signals
	WinHandle stop_listening_signal;

	//WinHandle reconnectThread;
	WinHandle captureThread;

	bool isASIOActive = false;
	bool reconnecting = false;
	bool previouslyFailed = false;
	bool useDeviceTiming = false;

	std::string get_id() {
		const void * address = static_cast<const void*>(source);
		std::stringstream ss;
		ss << "0x" << std::uppercase << std::hex << (int)address;
		std::string name = ss.str();
		return name;
	}

	asio_data() : source(NULL), first_ts(0), device_index(-1) {
		InitializeCriticalSection(&settings_mutex);

		memset(&route[0], -1, sizeof(long) * 8);

		stop_listening_signal = CreateEvent(nullptr, true, false, nullptr);
	}

	~asio_data() {
		DeleteCriticalSection(&settings_mutex);
		if (silent_buffer)
			bfree(silent_buffer);
	}

	bool disconnect() {
		isASIOActive = false;
		SetEvent(stop_listening_signal);
		if (captureThread.Valid())
			WaitForSingleObject(captureThread, INFINITE);
		ResetEvent(stop_listening_signal);
		return true;
	}

	bool render_audio(device_source_audio *asio_buffer, long route[]) {

		struct obs_audio_info aoi;
		obs_get_audio_info(&aoi);
		int index = BASS_ASIO_GetDevice();
		//blog(LOG_INFO, "dv index in render_audio is %i", index);
		obs_source_audio out;
		out.format = asio_buffer->format;
		if (!is_audio_planar(out.format)) {
			blog(LOG_ERROR, "that was a goof %i should be %i", out.format, get_planar_format(out.format));
			return false;
		}
		if (out.format == AUDIO_FORMAT_UNKNOWN) {
			blog(LOG_DEBUG, "unknown format");
			return false;
		}

		out.frames = asio_buffer->frames;
		out.samples_per_sec = asio_buffer->samples_per_sec;
		out.timestamp = asio_buffer->timestamp;
		if (!first_ts) {
			first_ts = out.timestamp;
			blog(LOG_DEBUG, "first timestamp");
			return false;
		}
		//cache a silent buffer
		size_t buffer_size = (out.frames * sizeof(bytedepth_format(out.format)));
		if (silent_buffer_size < buffer_size) {
			if (silent_buffer)
				bfree(silent_buffer);
			silent_buffer = (uint8_t*)bzalloc(buffer_size);
			silent_buffer_size = buffer_size;
			blog(LOG_DEBUG, "caching silent buffer");
		}

		if (unmuted_chs.size() == 0) {
			blog(LOG_DEBUG, "all chs muted");
			return 0;
		}

		for (short i = 0; i < aoi.speakers; i++) {
			if (route[i] >= 0 && route[i] < asio_buffer->input_chs) {
				out.data[i] = asio_buffer->data[route[i]];
			} else if (route[i] == -1) {
				out.data[i] = silent_buffer;
			} else {
				out.data[i] = silent_buffer;
			}
		}

		out.speakers = aoi.speakers;
		obs_source_output_audio(source, &out);
		//blog(LOG_DEBUG, "output frames %lu", buffer_size);
		return true;
	}

	static std::vector<short> _get_muted_chs(long route_array[]) {
		std::vector<short> silent_chs;
		silent_chs.reserve(MAX_AUDIO_CHANNELS);
		for (short i = 0; i < MAX_AUDIO_CHANNELS; i++) {
			if (route_array[i] == -1) {
				silent_chs.push_back(i);
			}
		}
		return silent_chs;
	}

	static std::vector<short> _get_unmuted_chs(long route_array[]) {
		std::vector<short> unmuted_chs;
		unmuted_chs.reserve(MAX_AUDIO_CHANNELS);
		for (short i = 0; i < MAX_AUDIO_CHANNELS; i++) {
			if (route_array[i] >= 0) {
				unmuted_chs.push_back(i);
			}
		}
		return unmuted_chs;
	}

};

class device_data {
private:
	size_t write_index;
	size_t buffer_count;

	size_t buffer_size;
	uint32_t frames;
	long input_chs;
	audio_format format;
	//not in use...
	WinHandle *receive_signals;
	WinHandle all_recieved_signal;
	WinHandle stop_listening_signal;

	bool all_prepped = false;
	bool buffer_prepped = false;
	bool circle_buffer_prepped = false;
	bool reallocate_buffer = false;
	bool events_prepped = false;

	circlebuf audio_buffer;
public:
	uint32_t samples_per_sec;

	long get_input_channels() {
		return input_chs;
	}

	long device_index;
	BASS_ASIO_DEVICEINFO device_info;

	device_source_audio* get_writeable_source_audio() {
		return (device_source_audio*)circlebuf_data(&audio_buffer,
				write_index * sizeof(device_source_audio));
	}

	device_source_audio* get_source_audio(size_t index) {
		return (device_source_audio*)circlebuf_data(&audio_buffer,
				index * sizeof(device_source_audio));
	}

	device_data() {
		all_prepped = false;
		buffer_prepped = false;
		circle_buffer_prepped = false;
		reallocate_buffer = false;
		events_prepped = false;

		format = AUDIO_FORMAT_UNKNOWN;
		write_index = 0;
		buffer_count = 32;

		all_recieved_signal = CreateEvent(nullptr, true, false, nullptr);
		stop_listening_signal = CreateEvent(nullptr, true, false, nullptr);
	}

	device_data(size_t buffers, audio_format audioformat) {
		all_prepped = false;
		buffer_prepped = false;
		circle_buffer_prepped = false;
		reallocate_buffer = false;
		events_prepped = false;

		format = audioformat;
		write_index = 0;
		buffer_count = buffers ? buffers : 32;

		all_recieved_signal = CreateEvent(nullptr, true, false, nullptr);
		stop_listening_signal = CreateEvent(nullptr, true, false, nullptr);
	}

	~device_data() {
		//free resources?
		if (all_prepped) {
			delete receive_signals;
			for (int i = 0; i < buffer_count; i++) {
				device_source_audio* _source_audio = get_source_audio(i);
				int input_chs = _source_audio->input_chs;
				for (int j = 0; j < input_chs; j++) {
					if (_source_audio->data[j])
						bfree(_source_audio->data[j]);
				}
				bfree(_source_audio->data);
			}
			circlebuf_free(&audio_buffer);
		}
	}

	//check that all the required device settings have been set
	void check_all() {
		if (buffer_prepped && circle_buffer_prepped && events_prepped) {
			all_prepped = true;
		} else {
			all_prepped = false;
		}
	}

	void prep_circle_buffer(BASS_ASIO_INFO &info) {
		prep_circle_buffer(info.bufpref);
	}

	void prep_circle_buffer(DWORD bufpref) {
		if (!circle_buffer_prepped) {
			//create a buffer w/ a minimum of 4 slots and a target of a fraction of 2048 samples
			buffer_count = max(4, ceil(2048 / bufpref));
			circlebuf_init(&audio_buffer);
			circlebuf_reserve(&audio_buffer, buffer_count * sizeof(device_source_audio));
			for (int i = 0; i < buffer_count; i++) {
				circlebuf_push_back(&audio_buffer,
						&device_source_audio(),
						sizeof(device_source_audio));
			}
			circle_buffer_prepped = true;
		}
	}

	void prep_events(BASS_ASIO_INFO &info) {
		prep_events(info.inputs);
	}

	void prep_events(long input_chs) {
		if (!events_prepped) {
			receive_signals = (WinHandle*)calloc(input_chs,
					sizeof(WinHandle));
			for (int i = 0; i < input_chs; i++)
				receive_signals[i] = CreateEvent(nullptr, true,
						false, nullptr);
			events_prepped = true;
		}
	}

	void re_prep_buffers() {
		all_prepped = false;
		buffer_prepped = false;
		BASS_ASIO_INFO info;
		bool ret = BASS_ASIO_GetInfo(&info);
		prep_buffers(info, format, samples_per_sec);
	}
	
	void re_prep_buffers(BASS_ASIO_INFO &info) {
		all_prepped = false;
		prep_buffers(info, format, samples_per_sec);
	}

	void update_sample_rate(uint32_t in_samples_per_sec) {
		all_prepped = false;
		this->samples_per_sec = in_samples_per_sec;
		check_all();
	}

	void prep_buffers(BASS_ASIO_INFO &info, audio_format in_format, uint32_t in_samples_per_sec) {
		prep_buffers(info.bufpref, info.inputs, in_format,
				in_samples_per_sec);
	}

	void prep_buffers(uint32_t frames, long in_chs, audio_format format, uint32_t samples_per_sec) {
		if (frames * bytedepth_format(format) > this->buffer_size) {
			if (buffer_prepped)
				reallocate_buffer = true;
		} else {
			reallocate_buffer = false;
		}
		prep_events(in_chs);
		if (circle_buffer_prepped && (!buffer_prepped || reallocate_buffer)) {
			this->frames = frames;
			this->input_chs = in_chs;
			this->format = format;
			this->samples_per_sec = samples_per_sec;
			this->buffer_size = frames * bytedepth_format(format);

			for (int i = 0; i < buffer_count; i++) {
				device_source_audio* _source_audio = get_source_audio(i);
				_source_audio->data = (uint8_t **)bzalloc(input_chs * sizeof(uint8_t*))/*calloc(input_chs, sizeof(uint8_t*))*/;
				for (int j = 0; j < input_chs; j++) {
					if (!buffer_prepped) {
						_source_audio->data[j] = (uint8_t*)bzalloc(buffer_size)/*calloc(buffer_size, 1)*/;
					} else if (reallocate_buffer) {
						uint8_t* tmp = (uint8_t*)realloc(_source_audio->data[j], buffer_size);
						if (tmp == NULL) {
							buffer_prepped = false;
							all_prepped = false;
							return;
						} else if (tmp == _source_audio->data[j]) {
							free(tmp);
							tmp = NULL;
						} else {
							_source_audio->data[j] = tmp;
							tmp = NULL;
						}
					}
				}
				_source_audio->input_chs = input_chs;
				_source_audio->frames = frames;
				_source_audio->format = format;
				_source_audio->samples_per_sec = samples_per_sec;
			}
			buffer_prepped = true;
		}
		check_all();
	}

	void write_buffer_interleaved(void* buffer, DWORD BufSize) {
		if (!all_prepped) {
			blog(LOG_DEBUG, "%s device %i is not prepared", __FUNCTION__, device_index);
			return;
		}
		SetEvent(all_recieved_signal);
		//get as much information from the device that called this function
		BASS_ASIO_INFO info;
		bool ret = BASS_ASIO_GetInfo(&info);
		uint8_t * input_buffer = (uint8_t*)buffer;
		size_t ch_buffer_size = BufSize / info.inputs;
		if (ch_buffer_size > buffer_size)
			blog(LOG_WARNING, "%s device needs to reallocate memory");

		int byte_depth = bytedepth_format(format);
		size_t interleaved_frame_size = info.inputs * byte_depth;
		//calculate on the spot
		size_t frames_count = BufSize / interleaved_frame_size;

		device_source_audio* _source_audio = get_writeable_source_audio();
		if (!_source_audio) {
			blog(LOG_DEBUG, "%s _source_audio = NULL", __FUNCTION__);
			ResetEvent(all_recieved_signal);
			return;
		}

		audio_format planar_format = get_planar_format(format);
		//deinterleave directly into buffer (planar)
		for (size_t i = 0; i < frames_count; i++) {
			for (size_t j = 0; j < info.inputs; j++)
				memcpy(_source_audio->data[j] + (i * byte_depth),
						input_buffer + (j * byte_depth)
						+ (i * interleaved_frame_size),
						byte_depth);
		}
		_source_audio->format = planar_format;
		_source_audio->frames = frames_count;
		_source_audio->input_chs = info.inputs;
		_source_audio->samples_per_sec = samples_per_sec;
		_source_audio->timestamp = _source_audio->timestamp = os_gettime_ns() - ((_source_audio->frames * NSEC_PER_SEC) / _source_audio->samples_per_sec);

		write_index++;
		write_index = write_index % buffer_count;
		ResetEvent(all_recieved_signal);
	}

	static DWORD WINAPI capture_thread(void *data) {
		listener_pair *pair = static_cast<listener_pair*>(data);
		asio_data *source = pair->asio_listener;
		device_data *device = pair->device;
		struct obs_audio_info aoi;
		obs_get_audio_info(&aoi);

		std::string thread_name = "asio capture: ";
		thread_name += source->get_id() + " (" + device->device_info.name + ")";
		os_set_thread_name(thread_name.c_str());

		HANDLE signals_1[3] = { device->all_recieved_signal, device->stop_listening_signal, source->stop_listening_signal };

		long route[MAX_AUDIO_CHANNELS];
		for (short i = 0; i < aoi.speakers; i++)
			route[i] = source->route[i];

		source->isASIOActive = true;
		ResetEvent(source->stop_listening_signal);

		blog(LOG_DEBUG, "listener for device %lu created: source: %x", device->device_index, source->get_id());

		size_t read_index = device->write_index;
		int waitResult;

		uint64_t buffer_time = ((device->frames * NSEC_PER_SEC) / device->samples_per_sec);

		while (source && device) {
			waitResult = WaitForMultipleObjects(3, signals_1, false, INFINITE);
			if (waitResult == WAIT_OBJECT_0) {
				while (read_index != device->write_index) {
					device_source_audio* in = device->get_source_audio(read_index);
					source->render_audio(in, route);
					read_index++;
					read_index = read_index % device->buffer_count;
				} 
				if (source->device_index != device->device_index) {
					blog(LOG_INFO, "source device index %lu is not device index %lu", source->device_index, device->device_index);
					blog(LOG_INFO, "%s closing", thread_name.c_str());
					delete pair;
					return 0;
				} else if (!source->isASIOActive) {
					blog(LOG_INFO, "%s indicated it wanted to disconnect", thread_name.c_str());
					blog(LOG_INFO, "%s closing", thread_name.c_str());
					delete pair;
					return 0;
				}
			} else if (waitResult == WAIT_OBJECT_0 + 1) {
				blog(LOG_INFO, "%s indicated it wanted to disconnect", thread_name.c_str());
				blog(LOG_INFO, "%s closing", thread_name.c_str());
				delete pair;
				return 0;
			} else if (waitResult == WAIT_OBJECT_0 + 2) {
				blog(LOG_INFO, "%s indicated it wanted to disconnect", thread_name.c_str());
				blog(LOG_INFO, "%s closing", thread_name.c_str());
				delete pair;
				return 0;
			} else if (waitResult == WAIT_ABANDONED_0) {
				blog(LOG_WARNING, "a mutex for %s was abandoned while listening to %l", thread_name.c_str(), device->device_index);
				blog(LOG_INFO, "%s closing", thread_name.c_str());
				delete pair;
				return 0;
			} else if (waitResult == WAIT_ABANDONED_0 + 1) {
				blog(LOG_WARNING, "a mutex for %s was abandoned while listening to %l", thread_name.c_str(), device->device_index);
				blog(LOG_INFO, "%s closing", thread_name.c_str());
				delete pair;
				return 0;
			} else if (waitResult == WAIT_ABANDONED_0 + 2) {
				blog(LOG_WARNING, "a mutex for %s was abandoned while listening to %l", thread_name.c_str(), device->device_index);
				blog(LOG_INFO, "%s closing", thread_name.c_str());
				delete pair;
				return 0;
			} else if (waitResult == WAIT_TIMEOUT) {
				blog(LOG_WARNING, "%s timed out while listening to %l", thread_name.c_str(), device->device_index);
				blog(LOG_INFO, "%s closing", thread_name.c_str());
				delete pair;
				return 0;
			} else if (waitResult == WAIT_FAILED) {
				blog(LOG_WARNING, "%s wait failed with 0x%x", thread_name.c_str(), GetLastError());
				blog(LOG_INFO, "%s closing", thread_name.c_str());
				delete pair;
				return 0;
			} else {
				blog(LOG_WARNING, "%s unexpected wait result = %i", thread_name.c_str(), waitResult);
				blog(LOG_INFO, "%s closing", thread_name.c_str());
				delete pair;
				return 0;
			}

		}
		delete pair;
		return 0;
	}

	//adds a listener thread between an asio_data object and this device
	void add_listener(asio_data *listener) {
		if (!all_prepped)
			return;
		listener_pair* parameters = new listener_pair();

		parameters->asio_listener = listener;
		parameters->device = this;
		blog(LOG_DEBUG, "disconnecting (source_id: %x)",
				listener->get_id());
		listener->disconnect();
		blog(LOG_DEBUG, "adding listener for %lu (source: %lu)",
				device_index, listener->device_index);
		listener->captureThread = CreateThread(nullptr, 0,
				this->capture_thread, parameters, 0, nullptr);
	}
};

std::vector<device_data*> device_list;

/*****************************************************************************/
// get number of output channels
DWORD get_obs_output_channels() {
	// get channel number from output speaker layout set by obs
	struct obs_audio_info aoi;
	obs_get_audio_info(&aoi);
	DWORD recorded_channels = get_audio_channels(aoi.speakers);
	return recorded_channels;
}

// get device number
uint8_t getDeviceCount() {
	uint8_t i;
	BASS_ASIO_DEVICEINFO info;
	for (i = 0; BASS_ASIO_GetDeviceInfo(i, &info); i++) {
		blog(LOG_DEBUG, "device index is : %i and name is : %s", i,
				info.name);
	}
	return i;
}

// get the device index from a device name : the current index can be retrieved from DWORD BASS_ASIO_GetDevice();
DWORD get_device_index(const char *device_info_name) {
	int res;
	BASS_ASIO_SetUnicode(false);
	BASS_ASIO_DEVICEINFO info;
	uint32_t i;
	for (i = 0; BASS_ASIO_GetDeviceInfo(i, &info); i++) {
		res = strcmp(info.name, device_info_name);
		if (res == 0)
			return i;
	}
	return -1;
}

DWORD get_device_index(BASS_ASIO_DEVICEINFO device_info) {
	return get_device_index(device_info.name);
}

bool is_device_index_valid(DWORD index) {
	return index < getDeviceCount();
}

DWORD get_device_buffer_index(BASS_ASIO_DEVICEINFO device_info) {
	uint32_t i;
	for (i = 0; i < device_list.size(); i++) {
		if (strcmp(device_list[i]->device_info.name,
				device_info.name) == 0)
			return i;
	}
	return -1;
}

// call the control panel
static bool DeviceControlPanel(obs_properties_t *props,
	obs_property_t *property, void *data) {
	if (!BASS_ASIO_ControlPanel()) {
		switch (BASS_ASIO_ErrorGetCode()) {
		case BASS_ERROR_INIT:
			blog(LOG_ERROR, "Init not called\n");
			break;
		case BASS_ERROR_UNKNOWN:
			blog(LOG_ERROR, "Unknown error\n");
			break;
		}
		return false;
	} else {
		int device_index = BASS_ASIO_GetDevice();
		BASS_ASIO_INFO info;
		BASS_ASIO_GetInfo(&info);
		blog(LOG_INFO, "Console loaded for device %s with index %i\n",
				info.name, device_index);
	}
	return true;
}

/*****************************************************************************/
void asio_update(void *vptr, obs_data_t *settings);
void asio_destroy(void *vptr);

void fill_out_devices(obs_property_t *list) {
	int res;
	BASS_ASIO_SetUnicode(false);
	BASS_ASIO_DEVICEINFO devinfo;
	bool ret;
	uint32_t i;
	for (i = 0; BASS_ASIO_GetDeviceInfo(i, &devinfo); i++)
		obs_property_list_add_string(list, devinfo.name, devinfo.name);
}

/* Creates list of input channels ; a muted channel has route value -1 and
* is recorded. The user can unmute the channel later.
*/
static bool fill_out_channels_modified(obs_properties_t *props, obs_property_t *list, obs_data_t *settings) {
	BASS_ASIO_INFO info;
	bool ret = BASS_ASIO_GetInfo(&info);
	BASS_ASIO_DEVICEINFO devinfo;
	int index = BASS_ASIO_GetDevice();
	ret = BASS_ASIO_GetDeviceInfo(index, &devinfo);
	if (!ret)
		blog(LOG_ERROR, "Unable to retrieve info on the current driver \nerror number is : %i \n; check BASS_ASIO_ErrorGetCode\n",
				BASS_ASIO_ErrorGetCode());
	// DEBUG: check that the current device in bass thread is the correct one
	// once code is fine the check can be removed
	const char* device = obs_data_get_string(settings, "device_id");
	if (strcmp(device, devinfo.name) != 0)
		blog(LOG_ERROR, "Device loaded is not the one in settings\n%s != %s", device, devinfo.name);

	DWORD input_channels = info.inputs;
	obs_property_list_clear(list);
	obs_property_list_add_int(list, "mute", -1);
	BASS_ASIO_CHANNELINFO ch_info;
	for (DWORD i = 0; i < input_channels; i++) {
		BASS_ASIO_ChannelGetInfo(1, i, &ch_info);
		std::string test = info.name;
		test += " " + std::to_string(i) + " " + ch_info.name;
		obs_property_list_add_int(list, test.c_str(), i);
	}
	return true;
}

//creates list of input sample rates supported by the device and OBS (obs supports only 44100 and 48000)
static bool fill_out_sample_rates(obs_properties_t *props, obs_property_t *list, obs_data_t *settings) {
	BASS_ASIO_DEVICEINFO devinfo;
	int index = BASS_ASIO_GetDevice();
	bool ret = BASS_ASIO_GetDeviceInfo(index, &devinfo);
	if (!ret)
		blog(LOG_ERROR, "Unable to retrieve info on the current driver \nerror number is : %i \n; check BASS_ASIO_ErrorGetCode\n",
				BASS_ASIO_ErrorGetCode());
	// DEBUG: check that the current device in bass thread is the correct one
	// once code is fine the check can be removed
	const char* device = obs_data_get_string(settings, "device_id");
	if (strcmp(device, devinfo.name) != 0)
		blog(LOG_ERROR, "Device loaded is not the one in settings\n%s != %s", device, devinfo.name);

	obs_property_list_clear(list);
	//get the device info
	ret = BASS_ASIO_CheckRate(44100);
	if (ret) {
		std::string rate = "44100 Hz";
		obs_property_list_add_int(list, rate.c_str(), 44100);
	} else {
		blog(LOG_DEBUG, "Device loaded does not support 44100 Hz sample rate\n");
	}

	ret = BASS_ASIO_CheckRate(48000);
	if (ret) {
		std::string rate = "48000 Hz";
		obs_property_list_add_int(list, rate.c_str(), 48000);
	} else {
		blog(LOG_DEBUG, "Device loaded does not support 48000 Hz sample rate\n");
	}
	return true;
}

//create list of supported audio formats
static bool fill_out_bit_depths(obs_properties_t *props, obs_property_t *list, obs_data_t *settings) {
	BASS_ASIO_DEVICEINFO devinfo;
	int index = BASS_ASIO_GetDevice();
	bool ret = BASS_ASIO_GetDeviceInfo(index, &devinfo);
	if (!ret) {
		blog(LOG_ERROR, "Unable to retrieve info on the current driver \n"
			"error number is : %i \n; check BASS_ASIO_ErrorGetCode\n",
			BASS_ASIO_ErrorGetCode());
	}
	// DEBUG: check that the current device in bass thread is the correct one
	// once code is fine the check can be removed
	const char* device = obs_data_get_string(settings, "device_id");
	if (strcmp(device, devinfo.name) != 0)
		blog(LOG_ERROR, "Device loaded is not the one in settings\n%s != %s", device, devinfo.name);


	//get the device channel info
	BASS_ASIO_CHANNELINFO channelInfo;
	ret = BASS_ASIO_ChannelGetInfo(true, 0, &channelInfo);
	if (!ret) {
		blog(LOG_ERROR, "Unable to retrieve channel info on the current driver \n"
			"error number is : %i \n; check BASS_ASIO_ErrorGetCode\n",
			BASS_ASIO_ErrorGetCode());
	}

	obs_property_list_clear(list);
	//these settings are ignored, optimal is picked between float and native for least
	//amount of processing possible
	if (channelInfo.format == BASS_ASIO_FORMAT_16BIT) {
		obs_property_list_add_int(list, "16 bit (native)", AUDIO_FORMAT_16BIT);
		obs_property_list_add_int(list, "32 bit", AUDIO_FORMAT_32BIT);
		obs_property_list_add_int(list, "32 bit float", AUDIO_FORMAT_FLOAT);
	} else if (channelInfo.format == BASS_ASIO_FORMAT_32BIT) {
		obs_property_list_add_int(list, "16 bit", AUDIO_FORMAT_16BIT);
		obs_property_list_add_int(list, "32 bit (native)", AUDIO_FORMAT_32BIT);
		obs_property_list_add_int(list, "32 bit float", AUDIO_FORMAT_FLOAT);
	} else if (channelInfo.format == BASS_ASIO_FORMAT_FLOAT) {
		obs_property_list_add_int(list, "16 bit", AUDIO_FORMAT_16BIT);
		obs_property_list_add_int(list, "32 bit", AUDIO_FORMAT_32BIT);
		obs_property_list_add_int(list, "32 bit float (native)", AUDIO_FORMAT_FLOAT);
	} else {
		blog(LOG_ERROR, "Your device uses unsupported bit depth.\nOnly 16 bit, 32 bit signed int and 32 bit float are supported.\nChange accordingly your device settings.\nForcing bit depth to 32 bit float");
		obs_property_list_add_int(list, "32 bit float",
				AUDIO_FORMAT_FLOAT);
		return false;
	}
	return true;
}

//create list of device supported buffer sizes
static bool fill_out_buffer_sizes(obs_properties_t *props, obs_property_t *list, obs_data_t *settings) {
	BASS_ASIO_INFO info;
	bool ret = BASS_ASIO_GetInfo(&info);
	if (!ret)
		blog(LOG_ERROR, "Unable to retrieve info on the current driver \nerror number is : %i \n; check BASS_ASIO_ErrorGetCode\n",
				BASS_ASIO_ErrorGetCode());

	obs_property_list_clear(list);

	if (info.bufgran == -1) {
		long long gran_buffer = info.bufmin;
		while (gran_buffer <= info.bufmax) {
			int n = snprintf(NULL, 0, "%llu%s", gran_buffer, (gran_buffer == info.bufpref ? " (preferred)" : ""));
			if (n <= 0) {
				//problem...continuing on the loop
				gran_buffer *= 2;
				continue;
			}
			char * buf = (char*)bmalloc((n + 1) * sizeof(char));
			if (!buf) {
				//problem...continuing on the loop
				gran_buffer *= 2;
				continue;
			}
			int c = snprintf(buf, n + 1, "%llu%s", gran_buffer, (gran_buffer == info.bufpref ? " (preferred)" : ""));
			buf[n] = '\0';
			obs_property_list_add_int(list, buf, gran_buffer);
			bfree(buf);
			gran_buffer *= 2;
		}
	}
	else if (info.bufgran == 0) {
		size_t gran_buffer = info.bufmin;
		int n = snprintf(NULL, 0, "%llu%s", gran_buffer, (gran_buffer == info.bufpref ? " (preferred)" : ""));
		char * buf = (char*)bmalloc((n + 1) * sizeof(char));
		int c = snprintf(buf, n + 1, "%llu%s", gran_buffer, (gran_buffer == info.bufpref ? " (preferred)" : ""));
		buf[n] = '\0';
		obs_property_list_add_int(list, buf, gran_buffer);
		bfree(buf);
	} else if (info.bufgran > 0) {
		size_t gran_buffer = info.bufmin;
		while (gran_buffer <= info.bufmax) {
			int n = snprintf(NULL, 0, "%llu%s", gran_buffer, (gran_buffer == info.bufpref ? " (preferred)" : ""));
			if (n <= 0) {
				//problem...continuing on the loop
				gran_buffer += info.bufgran;
				continue;
			}
			char * buf = (char*)bmalloc((n + 1) * sizeof(char));
			if (!buf) {
				//problem...continuing on the loop
				gran_buffer += info.bufgran;
				continue;
			}
			int c = snprintf(buf, n + 1, "%llu%s", gran_buffer, (gran_buffer == info.bufpref ? " (preferred)" : ""));
			buf[n] = '\0';
			obs_property_list_add_int(list, buf, gran_buffer);
			bfree(buf);
			gran_buffer += info.bufgran;
		}
	}

	return true;
}

static bool asio_device_changed(obs_properties_t *props,
	obs_property_t *list, obs_data_t *settings)
{
	const char *curDeviceId = obs_data_get_string(settings, "device_id");
	obs_property_t *sample_rate = obs_properties_get(props, "sample rate");
	obs_property_t *bit_depth = obs_properties_get(props, "bit depth");
	obs_property_t *buffer_size = obs_properties_get(props, "buffer");
	// be sure to set device as current one

	size_t itemCount = obs_property_list_item_count(list);
	bool itemFound = false;

	for (size_t i = 0; i < itemCount; i++) {
		const char *DeviceId = obs_property_list_item_string(list, i);
		if (strcmp(DeviceId, curDeviceId) == 0) {
			itemFound = true;
			break;
		}
	}

	if (!itemFound) {
		obs_property_list_insert_string(list, 0, " ", curDeviceId);
		obs_property_list_item_disable(list, 0, true);
	}
	else {
		DWORD device_index = get_device_index(curDeviceId);
		bool ret = BASS_ASIO_SetDevice(device_index);
		if (!ret) {
			blog(LOG_ERROR, "Unable to set device %i\n", device_index);
			if (BASS_ASIO_ErrorGetCode() == BASS_ERROR_INIT) {
				BASS_ASIO_Init(device_index, BASS_ASIO_THREAD);
				BASS_ASIO_SetDevice(device_index);
			}
			else if (BASS_ASIO_ErrorGetCode() == BASS_ERROR_DEVICE) {
				blog(LOG_ERROR, "Device index is invalid\n");
			}
		}
		else {
			obs_property_list_clear(sample_rate);
			obs_property_list_clear(bit_depth);
			//fill out based on device's settings
			obs_property_list_clear(buffer_size);
			obs_property_set_modified_callback(sample_rate, fill_out_sample_rates);
			obs_property_set_modified_callback(bit_depth, fill_out_bit_depths);
			obs_property_set_modified_callback(buffer_size, fill_out_buffer_sizes);

		}
	}
	// get channel number from output speaker layout set by obs
	DWORD recorded_channels = get_obs_output_channels();

	obs_property_t *route[MAX_AUDIO_CHANNELS];
	if (itemFound) {
		for (unsigned int i = 0; i < recorded_channels; i++) {
			std::string name = "route " + std::to_string(i);
			route[i] = obs_properties_get(props, name.c_str());
			obs_property_list_clear(route[i]);
//			obs_data_set_default_int(settings, name.c_str(), -1); // default is muted channels
			obs_property_set_modified_callback(route[i], fill_out_channels_modified);
		}
	}

	return true;
}

int mix(uint8_t *inputBuffer, obs_source_audio *out, size_t bytes_per_ch, int route[], unsigned int recorded_device_chs = UINT_MAX) {
	DWORD recorded_channels = get_obs_output_channels();
	short j = 0;
	for (size_t i = 0; i < recorded_channels; i++) {
		if (route[i] > -1 && route[i] < (int)recorded_device_chs) {
			out->data[j++] = inputBuffer + route[i] * bytes_per_ch;
		}
		else if (route[i] == -1) {
			uint8_t * silent_buffer;
			silent_buffer = (uint8_t *)calloc(bytes_per_ch, 1);
			out->data[j++] = silent_buffer;
		}
	}
	return true;
}

DWORD CALLBACK create_asio_buffer(BOOL input, DWORD channel, void *buffer, DWORD BufSize, void *device_ptr) {
	BASS_ASIO_INFO info;
	bool ret = BASS_ASIO_GetInfo(&info);
	device_data *device = (device_data*)device_ptr;
	device->write_buffer_interleaved(buffer, BufSize);

	return 0;
}

void CALLBACK asio_device_setting_changed(DWORD notify, void *device_ptr) {
	device_data *device = (device_data*)device_ptr;
	BASS_ASIO_INFO info;
	bool ret = BASS_ASIO_GetInfo(&info);
	uint32_t new_sample_rate; 
	switch (notify) {
	case BASS_ASIO_NOTIFY_RATE:
		new_sample_rate = BASS_ASIO_GetRate();
		blog(LOG_WARNING, "device %l changed sample rate to %f", device->device_index , new_sample_rate);

		if (!ret) {
			blog(LOG_ERROR, "Unable to retrieve info on the current driver \n"
				"error number is : %i; \n check BASS_ASIO_ErrorGetCode\n",
				BASS_ASIO_ErrorGetCode());
		}
		BASS_ASIO_Stop();
		device->update_sample_rate(new_sample_rate);
		device->re_prep_buffers(info);
		ret = BASS_ASIO_Start(info.bufpref, info.inputs);
		if (!ret) {
			switch (BASS_ASIO_ErrorGetCode()) {
			case BASS_ERROR_INIT:
				blog(LOG_ERROR, "Error: Bass asio not initialized.\n");
				break;
			case BASS_ERROR_ALREADY:
				blog(LOG_ERROR, "Error: device already started\n");
				break;
			case BASS_ERROR_NOCHAN:
				blog(LOG_ERROR, "Error: channels have not been enabled so can not start\n");
				break;
			case BASS_ERROR_UNKNOWN:
			default:
				blog(LOG_ERROR, "ASIO init: Unknown error when trying to start the device\n");
				break;
			}
		}

		break;
	case BASS_ASIO_NOTIFY_RESET:
		blog(LOG_WARNING, "device %l requested a reset", device->device_index);
		// Reset
		if (!ret) {
			blog(LOG_ERROR, "Unable to retrieve info on the current driver \nerror number is : %i \n; check BASS_ASIO_ErrorGetCode\n",
					BASS_ASIO_ErrorGetCode());
		}
		BASS_ASIO_Stop();
		device->re_prep_buffers(info);
		ret = BASS_ASIO_Start(info.bufpref,info.inputs);
		if (!ret) {
			switch (BASS_ASIO_ErrorGetCode()) {
			case BASS_ERROR_INIT:
				blog(LOG_ERROR, "Error: Bass asio not initialized.\n");
				break;
			case BASS_ERROR_ALREADY:
				blog(LOG_ERROR, "Error: device already started\n");
				break;
			case BASS_ERROR_NOCHAN:
				blog(LOG_ERROR, "Error: channels have not been enabled so can not start\n");
				break;
			case BASS_ERROR_UNKNOWN:
			default:
				blog(LOG_ERROR, "ASIO init: Unknown error when trying to start the device\n");
				break;
			}
		}	
		break;
	}
}

void asio_init(struct asio_data *data)
{
	// get info, useful for debug
	BASS_ASIO_INFO info;
	bool ret = BASS_ASIO_GetInfo(&info);
	int index = BASS_ASIO_GetDevice();
	BASS_ASIO_DEVICEINFO devinfo;
	ret = BASS_ASIO_GetDeviceInfo(index, &devinfo);
	if (!ret)
		blog(LOG_ERROR, "Unable to retrieve info on the current driver \nerror number is : %i \n; check BASS_ASIO_ErrorGetCode\n",
				BASS_ASIO_ErrorGetCode());

	uint8_t deviceNumber = getDeviceCount();
	if (deviceNumber < 1) {
		blog(LOG_INFO, "\nNo audio devices found!\n");
		return;
	}
	//BASS_ASIO_GetCPU
	// get channel number from output speaker layout set by obs
	DWORD spawn_threads = info.inputs;
	//get_obs_output_channels();

	// check buffer size is legit; if not set it to bufpref
	// to be implemented : to avoid issues, force to bufpref
	// this ignores any setting; bufpref is most likely set in asio control panel
	// check channel setup
	DWORD checkrate = BASS_ASIO_GetRate();
	blog(LOG_DEBUG, "sample rate is set in device to %i.\n", checkrate);
	DWORD checkbitdepth = BASS_ASIO_ChannelGetFormat(true, 0);
	blog(LOG_DEBUG, "bitdepth is set in device to %i, format: %i.\n", bytedepth_format(checkbitdepth), checkbitdepth);
	//audio_format format = asio_to_obs_audio_format(checkbitdepth);

	//get the device_index
	DWORD device_index = index; //get_device_index(devinfo.name);

	//start asio device if it hasn't already been
	if (!BASS_ASIO_IsStarted()) {
		DWORD obs_optimal_format = BASS_ASIO_FORMAT_FLOAT;
		DWORD asio_native_format = BASS_ASIO_ChannelGetFormat(true, 0);
		DWORD selected_format;
		//pick the best format to use (trying to get float if possible)
		if (obs_optimal_format == asio_native_format) {
			//all good...I don't think this needs to happen
			ret = BASS_ASIO_ChannelSetFormat(true, 0, asio_native_format);
			if (!ret) {
				blog(LOG_ERROR, "ASIO: unable to use native format\nerror number: %i \n; check BASS_ASIO_ErrorGetCode\n",
						BASS_ASIO_ErrorGetCode());
				return;
			}
			selected_format = asio_native_format;
		} else {
			ret = BASS_ASIO_ChannelSetFormat(true, 0, obs_optimal_format);
			if (!ret) {
				blog(LOG_ERROR, "ASIO: unable to use optimal format (float)\nerror number: %i \n; check BASS_ASIO_ErrorGetCode\n",
						BASS_ASIO_ErrorGetCode());

				ret = BASS_ASIO_ChannelSetFormat(true, 0, asio_native_format);
				if (!ret) {
					blog(LOG_ERROR, "ASIO: unable to use native format\nerror number: %i \n; check BASS_ASIO_ErrorGetCode\n",
							BASS_ASIO_ErrorGetCode());
					return;
				}
				selected_format = asio_native_format;
			} else {
				selected_format = obs_optimal_format;
			}
		}

		blog(LOG_DEBUG, "(best) bitdepth supported %i", selected_format);
		//audio_format format = asio_to_obs_audio_format(selected_format);
		audio_format format = get_planar_format(asio_to_obs_audio_format(selected_format));

		ret = BASS_ASIO_SetNotify(asio_device_setting_changed, device_list[device_index]);

		// enable all chs and link to callback w/ the device buffer class
		if (info.inputs > 0) {
			ret = BASS_ASIO_ChannelEnable(true, 0, &create_asio_buffer, device_list[device_index]);//data
			for (DWORD i = 1; i < info.inputs; i++)
				BASS_ASIO_ChannelJoin(true, i, 0);
		}

		/*prep the device buffers*/
		blog(LOG_DEBUG, "prepping device %lu", device_index);
		device_list[device_index]->prep_circle_buffer(info);
		device_list[device_index]->prep_events(info);
		device_list[device_index]->prep_buffers(info.bufpref, info.inputs, format, checkrate);

		/*start the device w/ # of threads*/
		blog(LOG_DEBUG, "starting device %lu", device_index);
		ret = BASS_ASIO_Start(info.bufpref, spawn_threads);
		if(!ret){
			switch (BASS_ASIO_ErrorGetCode()) {
			case BASS_ERROR_INIT:
				blog(LOG_ERROR, "Error: Bass asio not initialized.\n");
				break;
			case BASS_ERROR_ALREADY:
				blog(LOG_ERROR, "Error: device already started\n");
				break;
			case BASS_ERROR_NOCHAN:
				blog(LOG_ERROR, "Error: channels have not been enabled so can not start\n");
				break;
			case BASS_ERROR_UNKNOWN:
			default:
				blog(LOG_ERROR, "ASIO init: Unknown error when trying to start the device\n");
				break;
			}
		}
	}

	//Connect listener thread
	//data->captureThread =  device_list[device_index]->capture_thread();
	blog(LOG_DEBUG, "starting listener thread for: %lu", device_index);
	device_list[device_index]->add_listener(data);
}

static void * asio_create(obs_data_t *settings, obs_source_t *source)
{
	asio_data *data = new asio_data;

	data->source = source;
	data->first_ts = 0;
	data->device = NULL;

	asio_update(data, settings);
	blog(LOG_INFO, "Device '%s' created", data->device);

	return data;
}

void asio_destroy(void *vptr)
{
	struct asio_data *data = (asio_data *)vptr;
	blog(LOG_INFO, "Device '%s' destroyed", data->device);
	if (data) {
		bfree((void*)data->device);
		if (data->device_index < device_list.size()) {
			device_data *device = device_list[data->device_index];
			data->disconnect();
		}
	}
	delete data;
}

/* set all settings to asio_data struct and pass to driver */
void asio_update(void *vptr, obs_data_t *settings)
{
	struct asio_data *data = (asio_data *)vptr;
	const char *device;
	unsigned int rate;
	audio_format BitDepth;
	uint16_t BufferSize;
	unsigned int channels;
	BASS_ASIO_INFO info;
	int res;
	bool ret;
	DWORD route[MAX_AUDIO_CHANNELS];
	DWORD device_index;
	int numDevices = getDeviceCount();
	bool device_changed = false;
	const char *prev_device;
	DWORD prev_device_index;
	// lock down the settings mutex (protect against a sudden change when reading buffers)
	//EnterCriticalSection(&data->settings_mutex);
	// get channel number from output speaker layout set by obs
	DWORD recorded_channels = get_obs_output_channels();
	data->recorded_channels = recorded_channels;

	// get device from settings
	device = obs_data_get_string(settings, "device_id");

	if (device == NULL || device[0] == '\0') {
		blog(LOG_DEBUG, "Device not yet set \n");
	} else if (data->device == NULL || data->device[0] == '\0') {
		data->device = bstrdup(device);
	} else {
		if (strcmp(device, data->device) != 0) {
			prev_device = bstrdup(data->device);
			data->device = bstrdup(device);
			device_changed = true;
		}
	}

	if (device != NULL && device[0] != '\0') {
		device_index = get_device_index(device);
		if (!device_changed) {
			prev_device_index = device_index;
		}
		else {
			prev_device_index = get_device_index(prev_device);
		}
		// check if device is already initialized
		ret = BASS_ASIO_Init(device_index, BASS_ASIO_THREAD);
		bool first_initialization = false;

		if (!ret) {
			res = BASS_ASIO_ErrorGetCode();
			switch (res) {
			case BASS_ERROR_DEVICE:
				blog(LOG_ERROR, "The device number specified is invalid.\n");
				break;
			case BASS_ERROR_ALREADY:
				blog(LOG_ERROR, "The device has already been initialized\n");
				break;
			case BASS_ERROR_DRIVER:
				blog(LOG_ERROR, "The driver could not be initialized\n");
				break;
			}
		} else {
			blog(LOG_DEBUG, "Device %i was successfully initialized\n", device_index);
			first_initialization = true;
		}

		ret = BASS_ASIO_SetDevice(device_index);
		if (!ret) {
			res = BASS_ASIO_ErrorGetCode();
			switch (res) {
			case BASS_ERROR_DEVICE:
				blog(LOG_ERROR, "The device number specified is invalid.\n");
				break;
			case BASS_ERROR_INIT:
				blog(LOG_ERROR, "The device has not been initialized\n");
				break;
			}
		}

		ret = BASS_ASIO_GetInfo(&info);
		if (!ret)
			blog(LOG_ERROR, "Unable to retrieve info on the current driver \ndriver is not initialized\n");

		// DEBUG: check that the current device in bass thread is the correct one
		// once code is fine the check can be removed
		BASS_ASIO_DEVICEINFO devinfo;
		int index = BASS_ASIO_GetDevice();
		ret = BASS_ASIO_GetDeviceInfo(index, &devinfo);
		if (strcmp(device, devinfo.name) != 0)
			blog(LOG_ERROR, "Device loaded is not the one in settings\n%s != %s", device, devinfo.name);

		bool route_changed = false;
		for (unsigned int i = 0; i < recorded_channels; i++) {
			std::string route_str = "route " + std::to_string(i);
			route[i] = (int)obs_data_get_int(settings, route_str.c_str());
			if (data->route[i] != route[i]) {
				data->route[i] = route[i];
				route_changed = true;
			}
		}

		data->input_channels = info.inputs;
		data->output_channels = info.outputs;
		data->device_index = device_index;

		data->muted_chs = data->_get_muted_chs(data->route);
		data->unmuted_chs = data->_get_unmuted_chs(data->route);

		//spin up the asio device if it hasn't already and create a listener thread
		asio_init(data);
	}

}

const char * asio_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("asioInput");
}

void asio_get_defaults(obs_data_t *settings)
{
	struct obs_audio_info aoi;
	obs_get_audio_info(&aoi);
	obs_data_set_default_int(settings, "sample rate", aoi.samples_per_sec);
	obs_data_set_default_int(settings, "bit depth", AUDIO_FORMAT_FLOAT);
	DWORD recorded_channels = get_obs_output_channels();
	for (unsigned int i = 0; i < recorded_channels; i++) {
		std::string name = "route " + std::to_string(i);
		obs_data_set_default_int(settings, name.c_str(), -1); // default is muted channels
	}
}

obs_properties_t * asio_get_properties(void *unused)
{
	obs_properties_t *props;
	obs_property_t *devices;
	obs_property_t *rate;
	obs_property_t *bit_depth;
	obs_property_t *buffer_size;
	obs_property_t *route[MAX_AUDIO_CHANNELS];
	obs_property_t *console;

	UNUSED_PARAMETER(unused);

	props = obs_properties_create();
	devices = obs_properties_add_list(props, "device_id",
		obs_module_text("Device"), OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_STRING);
	obs_property_set_modified_callback(devices, asio_device_changed);
	fill_out_devices(devices);
	std::string dev_descr = "ASIO devices.\n"
		"OBS-Studio supports for now a single ASIO source.\n"
		"But duplication of an ASIO source in different scenes is still possible";
	obs_property_set_long_description(devices, dev_descr.c_str());
	// get channel number from output speaker layout set by obs
	DWORD recorded_channels = get_obs_output_channels();

	std::string route_descr = "For each OBS output channel, pick one\n of the input channels of your ASIO device.\n";
	std::string route_name;
	std::string route_obs;
	for (size_t i = 0; i < recorded_channels; i++) {
		route_name = "route " + std::to_string(i);
		route_obs = "Route." + std::to_string(i);
		route[i] = obs_properties_add_list(props, route_name.c_str(),
				obs_module_text(route_obs.c_str()),
				OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		obs_property_set_long_description(route[i], route_descr.c_str());
	}

	rate = obs_properties_add_list(props, "sample rate",
		obs_module_text("SampleRate"), OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_INT);
	std::string rate_descr = "Sample rate : number of samples per channel in one second.\n";
	obs_property_set_long_description(rate, rate_descr.c_str());

	bit_depth = obs_properties_add_list(props, "bit depth", TEXT_BITDEPTH,
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	std::string bit_descr = "Bit depth : size of a sample in bits and format.\nFloat should be preferred.";
	obs_property_set_long_description(bit_depth, bit_descr.c_str());

	buffer_size = obs_properties_add_list(props, "buffer", TEXT_BUFFER_SIZE,
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	//these should be based on the device
	obs_property_list_add_int(buffer_size, "64", 64);
	obs_property_list_add_int(buffer_size, "128", 128);
	obs_property_list_add_int(buffer_size, "256", 256);
	obs_property_list_add_int(buffer_size, "512", 512);
	obs_property_list_add_int(buffer_size, "1024", 1024);

	std::string buffer_descr = "Buffer : number of samples in a single frame.\nA lower value implies lower latency.\n256 should be OK for most cards.\nWarning: the real buffer returned by the device may differ";
	obs_property_set_long_description(buffer_size, buffer_descr.c_str());

	console = obs_properties_add_button(props, "console",
		obs_module_text("ASIO driver control panel"), DeviceControlPanel);
	std::string console_descr = "Make sure your settings in the Driver Control Panel\nfor sample rate and buffer are consistent with what you\nhave set in OBS.";
	obs_property_set_long_description(console, console_descr.c_str());

	return props;
}

bool obs_module_load(void)
{
	struct obs_source_info asio_input_capture = {};
	asio_input_capture.id = "win_asio";
	asio_input_capture.type = OBS_SOURCE_TYPE_INPUT;
	asio_input_capture.output_flags = OBS_SOURCE_AUDIO;
	asio_input_capture.create = asio_create;
	asio_input_capture.destroy = asio_destroy;
	asio_input_capture.update = asio_update;
	asio_input_capture.get_defaults = asio_get_defaults;
	asio_input_capture.get_name = asio_get_name;
	asio_input_capture.get_properties = asio_get_properties;

	uint8_t devices = getDeviceCount();
	device_list.reserve(devices);
	for (uint8_t i = 0; i < devices; i++) {
		device_data *device = new device_data();
		device->device_index = i;
		BASS_ASIO_GetDeviceInfo(i, &device->device_info);
		device_list.push_back(device);
	}

	obs_register_source(&asio_input_capture);
	return true;
}

void obs_module_unload(void){
	for (uint8_t i = 0; i < device_list.size(); i++) {
		/* stop streams */
		BASS_ASIO_SetDevice(i);
		BASS_ASIO_Stop();
		BASS_ASIO_Free();
		/* clear buffers */
		delete device_list[i];
	}
}
