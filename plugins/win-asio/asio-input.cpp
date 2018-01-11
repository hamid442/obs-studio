/*
Copyright (C) 2017 by pkv <pkv.stream@gmail.com>, andersama <???>

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
#include <obs-module.h>
#include <vector>
#include <stdio.h>
#include <string>
#include <windows.h>

#include "RtAudio.h"


OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("win-asio", "en-US")

#define blog(level, msg, ...) blog(level, "asio-input: " msg, ##__VA_ARGS__)

#define NSEC_PER_SEC  1000000000LL

#define TEXT_ROUTE_0                    obs_module_text("Route.0")
#define TEXT_ROUTE_1                    obs_module_text("Route.1")
#define TEXT_ROUTE_2                    obs_module_text("Route.2")
#define TEXT_ROUTE_3                    obs_module_text("Route.3")
#define TEXT_ROUTE_4                    obs_module_text("Route.4")
#define TEXT_ROUTE_5                    obs_module_text("Route.5")
#define TEXT_ROUTE_6                    obs_module_text("Route.6")
#define TEXT_ROUTE_7                    obs_module_text("Route.7")
#define TEXT_BUFFER_SIZE                obs_module_text("BufferSize")
#define TEXT_BUFFER_64_SAMPLES          obs_module_text("64_samples")
#define TEXT_BUFFER_128_SAMPLES         obs_module_text("128_samples")
#define TEXT_BUFFER_256_SAMPLES         obs_module_text("256_samples")
#define TEXT_BUFFER_512_SAMPLES         obs_module_text("512_samples")
#define TEXT_BUFFER_1024_SAMPLES        obs_module_text("1024_samples")
#define TEXT_BITDEPTH                   obs_module_text("BitDepth")


struct asio_data {
	obs_source_t *source;

	/*asio device and info */
	const char *device;
	uint8_t device_index;
	RtAudio::DeviceInfo info;
	RtAudio::StreamParameters parameters;

	audio_format BitDepth; // 16bit or 32 bit
	int SampleRate;          //44100 or 48000 Hz
	uint16_t BufferSize;     // number of samples in buffer
	uint64_t first_ts;       //first timestamp

	/* channels info */
	unsigned int channels; //total number of input channels
	unsigned int output_channels; // number of output channels (not used)
	unsigned int recorded_channels; // number of channels passed from device to OBS; is at most 8
	int route[8]; // stores the channel re-ordering info
};

/* global RtAudio */
RtAudio adc;

/* ======================================================================= */
/* conversion between rtaudio and obs */

enum audio_format rtasio_to_obs_audio_format(RtAudioFormat format)
{
	switch (format) {
	case RTAUDIO_SINT16:   return AUDIO_FORMAT_16BIT;
	case RTAUDIO_SINT32:   return AUDIO_FORMAT_32BIT;
	case RTAUDIO_FLOAT32:  return AUDIO_FORMAT_FLOAT;
	default:               break;
	}

	return AUDIO_FORMAT_UNKNOWN;
}

int BitDepthInBytes(audio_format format) {
	switch (format) {
	case AUDIO_FORMAT_16BIT:   return 2;
	case AUDIO_FORMAT_32BIT:   return 4;
	case AUDIO_FORMAT_FLOAT:
	default:                   return 4;
	}
}

RtAudioFormat obs_to_rtasio_audio_format(audio_format format)
{
	switch (format) {
	case AUDIO_FORMAT_16BIT:   return RTAUDIO_SINT16;
	// obs doesn't have 24 bit
	case AUDIO_FORMAT_32BIT:   return RTAUDIO_SINT32;
	case AUDIO_FORMAT_FLOAT:   return RTAUDIO_FLOAT32;
	default:                   return RTAUDIO_FLOAT32;
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

/*****************************************************************************/
//get device info
RtAudio::DeviceInfo get_device_info(const char *device) {
	RtAudio::DeviceInfo info;
	unsigned int numOfDevices = adc.getDeviceCount();
	for (uint8_t i = 0; i<numOfDevices; i++) {
		info = adc.getDeviceInfo(i);
		if (info.probed == true && strcmp(device, info.name.c_str()) == 0) {
			break;
		}
	}
	return info;
}

// get the device index
uint8_t get_device_index(const char *device) {
	RtAudio::DeviceInfo info;
	uint8_t device_index = 0;
	unsigned int numOfDevices = adc.getDeviceCount();
	for (uint8_t i = 0; i<numOfDevices; i++) {
		info = adc.getDeviceInfo(i);
		if (info.probed == true && strcmp(device, info.name.c_str()) == 0) {
			device_index = i;
			break;
		}
	}
	return device_index;
}

/*****************************************************************************/
//void asio_deinit(struct asio_data *data);
void asio_update(void *vptr, obs_data_t *settings);
void asio_destroy(void *vptr);

//creates the device list
void fill_out_devices(obs_property_t *list) {

	RtAudio::DeviceInfo info;
	std::vector<RtAudio::DeviceInfo> asioDeviceInfo;
	int numOfDevices = adc.getDeviceCount();
	char** names = new char*[numOfDevices];
	blog(LOG_INFO,"ASIO Devices: %i\n", numOfDevices);
	// Scan through devices for various capabilities
	for (int i = 0; i<numOfDevices; i++) {
		info = adc.getDeviceInfo(i);
		asioDeviceInfo.push_back(info);
		blog(LOG_INFO, "device  %i = %s and probed.true is %i\n", i, info.name.c_str(), info.probed);
		blog(LOG_INFO, ": maximum input channels = %i\n", info.inputChannels);
		blog(LOG_INFO, ": maximum output channels = %i\n", info.outputChannels);
		std::string test = info.name;
		char* cstr = new char[test.length() + 1];
		strcpy(cstr, test.c_str());
		names[i] = cstr;
	}

	//add devices to list 
	for (int i = 0; i < numOfDevices; i++) {
		blog(LOG_INFO, "list ASIO Devices: %i\n", numOfDevices);
		if (asioDeviceInfo[i].probed) {
			blog(LOG_INFO, "device %i  = %s added successfully.\n", i, names[i]);
			obs_property_list_add_string(list, names[i], names[i]);
		} else {
			blog(LOG_INFO, "device %i  = %s could not be added: driver issue.\n", i, names[i]);
		}

	}
}

/* Creates list of input channels ; modified so that value -1 means inactive channel.
 * This differs from a muted channel in that a muted channel would be passed to obs;
 * Here an inactive channel is not passed at all.
 */
static bool fill_out_channels_modified(obs_properties_t *props, obs_property_t *list, obs_data_t *settings) {
	const char* device = obs_data_get_string(settings, "device_id");
	RtAudio::DeviceInfo info;
	unsigned int input_channels;

	//get the device info
	info = get_device_info(device);
	input_channels = info.inputChannels;

	obs_property_list_clear(list);
	obs_property_list_add_int(list, "inactive", -1);
	for (unsigned int i = 0; i < input_channels; i++) {
		char** names = new char*[32];
		std::string test = info.name + " " + std::to_string(i);
		char* cstr = new char[test.length() + 1];
		strcpy(cstr, test.c_str());
		names[i] = cstr;
		+obs_property_list_add_int(list, names[i], i);
	}
	return true;
}

//creates list of input sample rates supported by the device
static bool fill_out_sample_rates(obs_properties_t *props, obs_property_t *list, obs_data_t *settings) {
	const char* device = obs_data_get_string(settings, "device_id");
	RtAudio::DeviceInfo info;
	unsigned int input_channels;

	obs_property_list_clear(list);
	//get the device info
	info = get_device_info(device);
	std::vector<unsigned int> sampleRates;
	sampleRates = info.sampleRates;
	size_t sampleRatesNb = sampleRates.size();
	for (unsigned int i = 0; i < sampleRatesNb; i++) {
		std::string rate = std::to_string(sampleRates[i]) + " Hz";
		char* cstr = new char[rate.length() + 1];
		strcpy(cstr, rate.c_str());
		obs_property_list_add_int(list, cstr, sampleRates[i]);
	}
	return true;
}

//create list of supported audio formats
static bool fill_out_bit_depths(obs_properties_t *props, obs_property_t *list, obs_data_t *settings) {
	const char* device = obs_data_get_string(settings, "device_id");
	RtAudio::DeviceInfo info;
	unsigned int input_channels;

	//get the device info
	info = get_device_info(device);
	RtAudioFormat nativeBitdepths;
	nativeBitdepths = info.nativeFormats;
	obs_property_list_clear(list);
	if (nativeBitdepths & 0x2) {
		obs_property_list_add_int(list, "16 bit (native)", AUDIO_FORMAT_16BIT_PLANAR);
		obs_property_list_add_int(list, "32 bit", AUDIO_FORMAT_32BIT_PLANAR);
		obs_property_list_add_int(list, "32 bit float", AUDIO_FORMAT_FLOAT_PLANAR);
	}
	else if (nativeBitdepths & 0x8) {
		obs_property_list_add_int(list, "16 bit", AUDIO_FORMAT_16BIT_PLANAR);
		obs_property_list_add_int(list, "32 bit (native)", AUDIO_FORMAT_32BIT_PLANAR);
		obs_property_list_add_int(list, "32 bit float", AUDIO_FORMAT_FLOAT_PLANAR);
	}
	else if (nativeBitdepths & 0x10) {
		obs_property_list_add_int(list, "16 bit", AUDIO_FORMAT_16BIT_PLANAR);
		obs_property_list_add_int(list, "32 bit", AUDIO_FORMAT_32BIT_PLANAR);
		obs_property_list_add_int(list, "32 bit float (native)", AUDIO_FORMAT_FLOAT_PLANAR);
	}

	return true;
}

static bool asio_device_changed(obs_properties_t *props,
	obs_property_t *list, obs_data_t *settings)
{
	const char *curDeviceId = obs_data_get_string(settings, "device_id");
	obs_property_t *sample_rate = obs_properties_get(props, "sample rate");
	obs_property_t *bit_depth = obs_properties_get(props, "bit depth");

	obs_property_t *route[8];
	for (unsigned int i = 0; i < 8; i++) {
		std::string name = "route " + std::to_string(i);
		route[i] = obs_properties_get(props, name.c_str());
		obs_property_list_clear(route[i]);
		obs_property_set_modified_callback(route[i], fill_out_channels_modified);
	}
	obs_property_list_clear(sample_rate);
	obs_property_list_clear(bit_depth);

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

	const char *defaultDeviceId = obs_data_get_default_string(settings, "device_id");
	if (curDeviceId == NULL || defaultDeviceId == NULL || (strcmp(defaultDeviceId,"") !=0 && strcmp(curDeviceId, defaultDeviceId) != 0)) {
		RtAudio::DeviceInfo info = get_device_info(curDeviceId);
		audio_format native_bit_depth = rtasio_to_obs_audio_format(info.nativeFormats);
		obs_data_set_int(settings, "bit depth", native_bit_depth);
	}
	obs_data_set_default_string(settings, "device_id", curDeviceId);

	obs_property_set_modified_callback(sample_rate, fill_out_sample_rates);
	obs_property_set_modified_callback(bit_depth, fill_out_bit_depths);

	return true;
}

int create_asio_buffer(void *outputBuffer, void *inputBuffer, unsigned int nBufferFrames,
	double streamTime, RtAudioStreamStatus status, void *userData) {
	unsigned int i;
	asio_data *data = (asio_data *)userData;
	int *route = data->route;
	// retrieve device info (for debug)
	RtAudio::DeviceInfo info = get_device_info(data->device);
	data->info = info;
	uint8_t *inputBuf = (uint8_t *)inputBuffer;
	int recorded_channels = data->recorded_channels;

	if (status) {
		blog(LOG_WARNING, "Stream overflow detected!");
		return 0;
	}

	// won't ever reach that part of code unless we 've made some severe coding error
	if (recorded_channels > 8) {
		blog(LOG_ERROR, "OBS does not support more than 8 channels");
		return 2;
	}

	/* buffer in Bytes =
	 * number of frames in buffer x number of channels x bitdepth / 8
	 * buffer per channel in Bytes = number of frames in buffer x bitdepth / 8                                                
	 */
	int BitDepthBytes = BitDepthInBytes(data->BitDepth);
	size_t bufSizePerChannelBytes = nBufferFrames * BitDepthBytes;
	size_t bufSizeBytes = bufSizePerChannelBytes * recorded_channels;
	size_t targetSizeBytes = bufSizePerChannelBytes * data->channels;
	if (recorded_channels == 7) { 
		bufSizeBytes = bufSizePerChannelBytes * 8;
	}


	/* RtAudio tries to allocate data->BufferSize but per API actual value may differ. 
	 * For instance this is the case for Rearoute driver: if we request 256 in OpenStream()
	 * we get 1024 instead for this callback.
	 */
	if (nBufferFrames > data->BufferSize) {
//		blog(LOG_WARNING, "Buffer is too small! %i > %i", nBufferFrames, data->BufferSize);
	}
	else if (nBufferFrames < data->BufferSize) {
//		blog(LOG_WARNING, "Buffer is too big! %i < %i", nBufferFrames, data->BufferSize);
	}
	else {
//		blog(LOG_WARNING, "Buffer is just right: %i", nBufferFrames);
	}

	if (status) {
		blog(LOG_WARNING, "Stream overflow detected!");
		return 0;
	}


	/* planar code */

	struct obs_source_audio out;
	int j = 0;
	for (size_t i = 0; i < MAX_AUDIO_CHANNELS; i++) {
		if (route[i] != -1) {
			out.data[j++] = inputBuf + route[i] * bufSizePerChannelBytes;
		}	
	}

	out.format = data->BitDepth;
	out.speakers = asio_channels_to_obs_speakers(recorded_channels);
	//to do, add a mute channel for ch = 7 
	//if (recorded_channels == 7) {
	//	out.speakers = SPEAKERS_7POINT1;
	//	out.data[7] = 
	//}
	out.samples_per_sec = data->SampleRate;
	out.frames = nBufferFrames;// beware, may differ from data->BufferSize;
	out.timestamp = os_gettime_ns() - ((nBufferFrames * NSEC_PER_SEC) / data->SampleRate);

	if (!data->first_ts) {
		data->first_ts = out.timestamp;
	}

	if (out.timestamp > data->first_ts && recorded_channels != 0) {
		obs_source_output_audio(data->source, &out);
	}

	return 0;
}

void asio_init(struct asio_data *data)
{
	// number of channels which will be captured
	int recorded_channels = data->recorded_channels;
	// get info, useful for debug
	RtAudio::DeviceInfo info = get_device_info(data->device);
	data->info = info;

	unsigned int deviceNumber = adc.getDeviceCount();
	if (deviceNumber < 1) {
		blog(LOG_INFO,"\nNo audio devices found!\n");
	}
	RtAudio::StreamParameters parameters;
	parameters.deviceId = data->device_index;
	parameters.nChannels = data->channels;
	parameters.firstChannel = 0; 
	unsigned int sampleRate = data->SampleRate;
	unsigned int bufferFrames = data->BufferSize;
	RtAudioFormat audioFormat = obs_to_rtasio_audio_format(data->BitDepth);
	//store StreamParameters in asio_data struct
	data->parameters = parameters;
	//force planar formats
	RtAudio::StreamOptions options;
	options.flags = RTAUDIO_NONINTERLEAVED;

	if (! adc.isStreamOpen() && data->recorded_channels != 0) {
		try {
			adc.openStream(NULL, &parameters, audioFormat, sampleRate, 
					&bufferFrames, &create_asio_buffer, data, &options);
		}
		catch (RtAudioError& e) {
			e.printMessage();
			blog(LOG_INFO, "error caught in openStream\n");
			blog(LOG_INFO, "error type number is %i\n", e.getType());
			blog(LOG_INFO, "error: %s\n", e.getMessage().c_str());
			goto cleanup;
		}
	}
	if (! adc.isStreamRunning()) {
		try {
			adc.startStream();
		}
		catch (RtAudioError& e) {
			e.printMessage();
			blog(LOG_INFO, "error caught in startStream\n");
			blog(LOG_INFO, "error type number is %i\n", e.getType());
			blog(LOG_INFO, "error: %s\n", e.getMessage().c_str());
			goto cleanup;
		}
	}
	return;

cleanup:
	try {
		adc.stopStream();
	}
	catch (RtAudioError& e) {
		e.printMessage();
		blog(LOG_ERROR, "error caught in stopStream");
		blog(LOG_INFO, "error type number is %i\n", e.getType());
		blog(LOG_INFO, "error: %s\n", e.getMessage().c_str());
	}
	if (adc.isStreamOpen())
		adc.closeStream();
}

static void * asio_create(obs_data_t *settings, obs_source_t *source)
{
	struct asio_data *data = new asio_data;
	
	data->source = source;
	data->first_ts = 0;
	data->device = NULL;

	asio_update(data, settings);

	if (obs_data_get_string(settings, "device_id")) {
		asio_init(data);
	}

	return data;
}

void asio_destroy(void *vptr)
{
	struct asio_data *data = (asio_data *)vptr;

	try {
		adc.stopStream();
	}
	catch (RtAudioError& e) {
		e.printMessage();
		blog(LOG_INFO, "error caught in asio_destroy()\n");
		blog(LOG_INFO, "error type number is %i\n", e.getType());
		blog(LOG_INFO, "error: %s\n", e.getMessage().c_str());
	}

	if (adc.isStreamOpen()) {
		adc.closeStream();
	}

	delete data;
}

/* set all settings to asio_data struct */
void asio_update(void *vptr, obs_data_t *settings)
{
	struct asio_data *data =(asio_data *)vptr;
	const char *device;
	unsigned int rate;
	speaker_layout ChannelFormat;
	audio_format BitDepth;
	uint16_t BufferSize;
	unsigned int channels;
	uint8_t FirstChannel;
	uint8_t LastChannel;
	RtAudio::DeviceInfo info;
	bool reset = false;
	int route[8];
	unsigned int recorded_channels = 0;

	// get device from settings
	device = obs_data_get_string(settings, "device_id");

	try {
		if (device == NULL) {
			reset = true;
		} else if (data->device == NULL) {
			data->device = bstrdup(device);
			reset = true;
		} else {
			if (strcmp(device, data->device) != 0) {
				data->device = bstrdup(device);
				reset = true;
			}
		}
	}
	catch(...) {
		blog(LOG_INFO, "Initializing asio device");
	}
	
	info = get_device_info(device);
	
	for (unsigned int i = 0; i < 8; i++) {
		std::string route_str = "route " + std::to_string(i);
		route[i] = obs_data_get_int(settings, route_str.c_str());
		if (data->route[i] != route[i]) {
			data->route[i] = route[i];
			reset = true;		
		}
		if (route[i] != -1)
			recorded_channels += 1;
	}
	data->recorded_channels = recorded_channels;

	rate = (int)obs_data_get_int(settings, "sample rate");
	if (data->SampleRate != (int)rate) {
		data->SampleRate = (int)rate;
		reset = true;
	}

	BitDepth = (audio_format)obs_data_get_int(settings,"bit depth");
	if (data->BitDepth != BitDepth) {
		data->BitDepth = BitDepth;
		reset = true;
	}

	BufferSize = obs_data_get_int(settings, "buffer");
	if (data->BufferSize != BufferSize) {
		data->BufferSize = BufferSize;
		reset = true;
	}

	data->channels = info.inputChannels;
	channels = data->channels;
	data->output_channels = info.outputChannels;
	data->device_index = get_device_index(device);

	if (reset && adc.isStreamOpen()) {
		if (adc.isStreamRunning()) {
			try {
				adc.stopStream();
			}
			catch (RtAudioError& e) {
				e.printMessage();
				blog(LOG_INFO, "error caught in asio_update()\n");
				blog(LOG_INFO, "error type number is %i\n", e.getType());
				blog(LOG_INFO, "error: %s\n", e.getMessage().c_str());
			}
		}
		adc.closeStream();
		asio_init(data);
	}
	else if(reset) {
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
	obs_data_set_default_int(settings, "sample rate", 48000);
	obs_data_set_default_int(settings, "bit depth", AUDIO_FORMAT_32BIT);
	obs_data_set_default_int(settings, "route 0", -1);
	obs_data_set_default_int(settings, "route 1", -1);
	obs_data_set_default_int(settings, "route 2", -1);
	obs_data_set_default_int(settings, "route 3", -1);
	obs_data_set_default_int(settings, "route 4", -1);
	obs_data_set_default_int(settings, "route 5", -1);
	obs_data_set_default_int(settings, "route 6", -1);
	obs_data_set_default_int(settings, "route 7", -1);
}

obs_properties_t * asio_get_properties(void *unused)
{
	obs_properties_t *props;
	obs_property_t *devices;
	obs_property_t *rate;
	obs_property_t *first_channel;
	obs_property_t *last_channel;
	obs_property_t *bit_depth;
	obs_property_t *buffer_size;
	obs_property_t *route[8];

	UNUSED_PARAMETER(unused);

	props = obs_properties_create();
	devices = obs_properties_add_list(props, "device_id",
			obs_module_text("Device"), OBS_COMBO_TYPE_LIST,
			OBS_COMBO_FORMAT_STRING);
	obs_property_set_modified_callback(devices, asio_device_changed);
	fill_out_devices(devices);

	route[0] = obs_properties_add_list(props, "route 0", TEXT_ROUTE_0,
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	route[1] = obs_properties_add_list(props, "route 1", TEXT_ROUTE_1,
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	route[2] = obs_properties_add_list(props, "route 2", TEXT_ROUTE_2,
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	route[3] = obs_properties_add_list(props, "route 3", TEXT_ROUTE_3,
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	route[4] = obs_properties_add_list(props, "route 4", TEXT_ROUTE_4,
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	route[5] = obs_properties_add_list(props, "route 5", TEXT_ROUTE_5,
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	route[6] = obs_properties_add_list(props, "route 6", TEXT_ROUTE_6,
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	route[7] = obs_properties_add_list(props, "route 7", TEXT_ROUTE_7,
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	//for (unsigned int i = 0; i < 8; i++) {
		//	obs_property_set_modified_callback(route[i], fill_out_channels_modified);
	//}

	rate = obs_properties_add_list(props, "sample rate",
			obs_module_text("SampleRate"), OBS_COMBO_TYPE_LIST,
			OBS_COMBO_FORMAT_INT);

	bit_depth = obs_properties_add_list(props, "bit depth",
			TEXT_BITDEPTH, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	
	buffer_size = obs_properties_add_list(props, "buffer", TEXT_BUFFER_SIZE,
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(buffer_size, "64", 64);
	obs_property_list_add_int(buffer_size, "128", 128);
	obs_property_list_add_int(buffer_size, "256", 256);
	obs_property_list_add_int(buffer_size, "512", 512);
	obs_property_list_add_int(buffer_size, "1024", 1024);

	return props;
}

bool obs_module_load(void)
{
	struct obs_source_info asio_input_capture = {};
	asio_input_capture.id             = "asio_input_capture";
	asio_input_capture.type           = OBS_SOURCE_TYPE_INPUT;
	asio_input_capture.output_flags   = OBS_SOURCE_AUDIO;
	asio_input_capture.create         = asio_create;
	asio_input_capture.destroy        = asio_destroy;
	asio_input_capture.update         = asio_update;
	asio_input_capture.get_defaults   = asio_get_defaults;
	asio_input_capture.get_name       = asio_get_name;
	asio_input_capture.get_properties = asio_get_properties;

	obs_register_source(&asio_input_capture);
	return true;
}
