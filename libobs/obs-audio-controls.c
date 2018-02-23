/*
Copyright (C) 2014 by Leonhard Oelke <leonhard@in-verted.de>

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

#include <math.h>

#include "util/threading.h"
#include "util/bmem.h"
#include "media-io/audio-math.h"
#include "obs.h"
#include "obs-internal.h"

#include "obs-audio-controls.h"

/* These are pointless warnings generated not by our code, but by a standard
 * library macro, INFINITY */
#ifdef _MSC_VER
#pragma warning(disable : 4056)
#pragma warning(disable : 4756)
#endif

#define CLAMP(x, min, max) ((x) < min ? min : ((x) > max ? max : (x)))

//https://en.wikipedia.org/wiki/Cooley–Tukey_FFT_algorithm#The_radix-2_DIT_case
// simplication for floating type
void seperate_fixed(float* a, uint32_t n) {
	static float tmp[AUDIO_OUTPUT_FRAMES];

	for (int i = 0; i<n / 2; i++)    // copy all odd elements to heap storage
		tmp[i] = a[i * 2 + 1];
	for (int i = 0; i<n / 2; i++)    // copy all even elements to lower-half of a[]
		a[i] = a[i * 2];
	for (int i = 0; i<n / 2; i++)    // copy all odd (from heap) to upper-half of a[]
		a[i + n / 2] = tmp[i];
}

#define M_PI       3.14159265358979323846   // pi
#define M_PI_2     1.57079632679489661923   // pi/2
#define _RE        0
#define _IM        1

// N must be a power-of-2, or bad things will happen.
// Currently no check for this condition.
//
// N input samples in X[] are FFT'd and results left in X[].
// Because of Nyquist theorem, N samples means 
// only first N/2 FFT results in X[] are the answer.
// (upper half of X[] is a reflection with no new information).
void _audio_fft_complex(float* X, float* Xi, int N) {
	if (N < 2) {
		// bottom of recursion.
		// Do nothing here, because already X[0] = x[0]
	}
	else {
		seperate_fixed(X, N);
		seperate_fixed(Xi, N);
		//half of N
		int hN = N / 2;
		double iHN = 2 / N;
		//w_r = 1.0 forever and always e^0 = 1
		//static const float w_r = 1.0;

		double w_r = 1.0;
		double w_i = 0.0;
		double q_exp = 0.0;
		_audio_fft_complex(X, Xi, hN);
		_audio_fft_complex(X + hN, Xi + hN, hN);


		for (int k = 0; k < hN; k++) {
			double e_r = X[k];
			double e_i = Xi[k];

			double o_r = X[k + hN];
			double o_i = Xi[k + hN];

			double t = -2.0*M_PI*k / N;
			w_r = cos(t);
			w_i = sin(t);
			//quick_exp(k, N); 
			//for audio inputs the imaginary component is 0
			//w * o;
			double wo_r = (w_r * o_r) - (w_i * o_i);
			double wo_i = (w_r * o_i) + (w_i * o_r);

			X[k] = e_r + wo_r; 
			X[k + hN] = e_r - wo_r;

			Xi[k] = e_i + wo_i;
			Xi[k + hN] = e_i - wo_i;
		}
	}
}

//stuffs complex results in other half
void audio_fft_complex_1024(float* X, int N) {
	float Xi[AUDIO_OUTPUT_FRAMES] = { 0 };
	_audio_fft_complex(X, Xi, N, N);
	int hN = N / 2;
	//copy the complex components
	memcpy(&(X[hN]), &(Xi[0]), hN*sizeof(float));
}


typedef float (*obs_fader_conversion_t)(const float val);

struct fader_cb {
	obs_fader_changed_t   callback;
	void                  *param;
};

struct obs_fader {
	pthread_mutex_t        mutex;
	obs_fader_conversion_t def_to_db;
	obs_fader_conversion_t db_to_def;
	obs_source_t           *source;
	enum obs_fader_type    type;
	float                  max_db;
	float                  min_db;
	float                  cur_db;
	bool                   ignore_next_signal;

	pthread_mutex_t        callback_mutex;
	DARRAY(struct fader_cb)callbacks;
};

struct meter_cb {
	obs_volmeter_updated_t callback;
	void                   *param;
};

struct obs_volmeter {
	pthread_mutex_t        mutex;
	obs_source_t           *source;
	enum obs_fader_type    type;
	float                  cur_db;

	pthread_mutex_t        callback_mutex;
	DARRAY(struct meter_cb)callbacks;

	unsigned int           update_ms;

	float                  vol_magnitude[MAX_AUDIO_CHANNELS];
	float                  vol_peak[MAX_AUDIO_CHANNELS];
	
	enum obs_volume_meter_type    volume_meter;
	enum obs_volume_meter_options volume_options;

	size_t                 circle_buffer_index;
	struct audio_data      circle_buffer;
	struct audio_data      fft_buffer;
};

static float cubic_def_to_db(const float def)
{
	if (def == 1.0f)
		return 0.0f;
	else if (def <= 0.0f)
		return -INFINITY;

	return mul_to_db(def * def * def);
}

static float cubic_db_to_def(const float db)
{
	if (db == 0.0f)
		return 1.0f;
	else if (db == -INFINITY)
		return 0.0f;

	return cbrtf(db_to_mul(db));
}

static float iec_def_to_db(const float def)
{
	if (def == 1.0f)
		return 0.0f;
	else if (def <= 0.0f)
		return -INFINITY;

	float db;

	if (def >= 0.75f)
		db = (def - 1.0f) / 0.25f * 9.0f;
	else if (def >= 0.5f)
		db = (def - 0.75f) / 0.25f * 11.0f - 9.0f;
	else if (def >= 0.3f)
		db = (def - 0.5f) / 0.2f * 10.0f - 20.0f;
	else if (def >= 0.15f)
		db = (def - 0.3f) / 0.15f * 10.0f - 30.0f;
	else if (def >= 0.075f)
		db = (def - 0.15f) / 0.075f * 10.0f - 40.0f;
	else if (def >= 0.025f)
		db = (def - 0.075f) / 0.05f * 10.0f - 50.0f;
	else if (def >= 0.001f)
		db = (def - 0.025f) / 0.025f * 90.0f - 60.0f;
	else
		db = -INFINITY;

	return db;
}

static float iec_db_to_def(const float db)
{
	if (db == 0.0f)
		return 1.0f;
	else if (db == -INFINITY)
		return 0.0f;

	float def;

	if (db >= -9.0f)
		def = (db + 9.0f) / 9.0f * 0.25f + 0.75f;
	else if (db >= -20.0f)
		def = (db + 20.0f) / 11.0f * 0.25f + 0.5f;
	else if (db >= -30.0f)
		def = (db + 30.0f) / 10.0f * 0.2f + 0.3f;
	else if (db >= -40.0f)
		def = (db + 40.0f) / 10.0f * 0.15f + 0.15f;
	else if (db >= -50.0f)
		def = (db + 50.0f) / 10.0f * 0.075f + 0.075f;
	else if (db >= -60.0f)
		def = (db + 60.0f) / 10.0f * 0.05f + 0.025f;
	else if (db >= -114.0f)
		def = (db + 150.0f) / 90.0f * 0.025f;
	else
		def = 0.0f;

	return def;
}

#define LOG_OFFSET_DB  6.0f
#define LOG_RANGE_DB   96.0f
/* equals -log10f(LOG_OFFSET_DB) */
#define LOG_OFFSET_VAL -0.77815125038364363f
/* equals -log10f(-LOG_RANGE_DB + LOG_OFFSET_DB) */
#define LOG_RANGE_VAL  -2.00860017176191756f

static float log_def_to_db(const float def)
{
	if (def >= 1.0f)
		return 0.0f;
	else if (def <= 0.0f)
		return -INFINITY;

	return -(LOG_RANGE_DB + LOG_OFFSET_DB) * powf(
			(LOG_RANGE_DB + LOG_OFFSET_DB) / LOG_OFFSET_DB, -def)
			+ LOG_OFFSET_DB;
}

static float log_db_to_def(const float db)
{
	if (db >= 0.0f)
		return 1.0f;
	else if (db <= -96.0f)
		return 0.0f;

	return (-log10f(-db + LOG_OFFSET_DB) - LOG_RANGE_VAL)
			/ (LOG_OFFSET_VAL - LOG_RANGE_VAL);
}

static void signal_volume_changed(struct obs_fader *fader, const float db)
{
	pthread_mutex_lock(&fader->callback_mutex);
	for (size_t i = fader->callbacks.num; i > 0; i--) {
		struct fader_cb cb = fader->callbacks.array[i - 1];
		cb.callback(cb.param, db);
	}
	pthread_mutex_unlock(&fader->callback_mutex);
}

static void signal_levels_updated(struct obs_volmeter *volmeter,
		const float magnitude[MAX_AUDIO_CHANNELS],
		const float peak[MAX_AUDIO_CHANNELS],
		const float input_peak[MAX_AUDIO_CHANNELS])
{
	pthread_mutex_lock(&volmeter->callback_mutex);
	for (size_t i = volmeter->callbacks.num; i > 0; i--) {
		struct meter_cb cb = volmeter->callbacks.array[i - 1];
		cb.callback(cb.param, magnitude, peak, input_peak, audio_buffer, fft_buffer);
	}
	pthread_mutex_unlock(&volmeter->callback_mutex);
}

static void fader_source_volume_changed(void *vptr, calldata_t *calldata)
{
	struct obs_fader *fader = (struct obs_fader *) vptr;

	pthread_mutex_lock(&fader->mutex);

	if (fader->ignore_next_signal) {
		fader->ignore_next_signal = false;
		pthread_mutex_unlock(&fader->mutex);
		return;
	}

	const float mul      = (float)calldata_float(calldata, "volume");
	const float db       = mul_to_db(mul);
	fader->cur_db        = db;

	pthread_mutex_unlock(&fader->mutex);

	signal_volume_changed(fader, db);
}

static void volmeter_source_volume_changed(void *vptr, calldata_t *calldata)
{
	struct obs_volmeter *volmeter = (struct obs_volmeter *) vptr;

	pthread_mutex_lock(&volmeter->mutex);

	float mul = (float) calldata_float(calldata, "volume");
	volmeter->cur_db = mul_to_db(mul);

	pthread_mutex_unlock(&volmeter->mutex);
}

static void fader_source_destroyed(void *vptr, calldata_t *calldata)
{
	UNUSED_PARAMETER(calldata);
	struct obs_fader *fader = (struct obs_fader *) vptr;

	obs_fader_detach_source(fader);
}

static void volmeter_source_destroyed(void *vptr, calldata_t *calldata)
{
	UNUSED_PARAMETER(calldata);
	struct obs_volmeter *volmeter = (struct obs_volmeter *) vptr;

	obs_volmeter_detach_source(volmeter);
}

static void volmeter_process_audio_data(obs_volmeter_t *volmeter,
		const struct audio_data *data)
{
	int nr_samples = data->frames;
	int channel_nr = 0;

	for (size_t plane_nr = 0; plane_nr < MAX_AV_PLANES; plane_nr++) {
		float *samples = (float *)data->data[plane_nr];
		if (!samples) {
			// This plane does not contain data.
			continue;
		}

		// For each plane calculate:
		// * peak = the maximum-absolute of the sample values.
		// * magnitude = root-mean-square of the sample values.
		//      A VU meter needs to integrate over 300ms, but this will
		//	be handled by the ballistics of the meter itself,
		//	reality. Which makes this calculation independent of
		//	sample rate or update rate.
		float peak = 0.0;
		float sum_of_squares = 0.0;
		for (int sample_nr = 0; sample_nr < nr_samples; sample_nr++) {
			float sample = samples[sample_nr];

			peak = fmaxf(peak, fabsf(sample));
			sum_of_squares += (sample * sample);
		}

		volmeter->vol_magnitude[channel_nr] = sqrtf(sum_of_squares /
			nr_samples);
		volmeter->vol_peak[channel_nr] = peak;
		channel_nr++;
		
		float *meter_samples = (float *)volmeter->circle_buffer.data[plane_nr];
		//copy new frames to the circle buffer
		for (int sample_nr = nr_samples-1; sample_nr >= 0; sample_nr--) {
			if (volmeter->circle_buffer_index == 0) {
				volmeter->circle_buffer_index = volmeter->circle_buffer.frames;
			}
			volmeter->circle_buffer_index--;
			volmeter->circle_buffer_index = volmeter->circle_buffer_index % volmeter->circle_buffer.frames;
			meter_samples[volmeter->circle_buffer_index] = samples[sample_nr];
		}
		
		//copy the data to the fft array for processing
		size_t second_chunk = (volmeter->circle_buffer_index) * sizeof(float);
		size_t first_index = volmeter->circle_buffer.frames - volmeter->circle_buffer_index;
		size_t first_chunk = (first_index) * sizeof(float);
		float *fft_samples = (float *)volmeter->fft_buffer.data[plane_nr];

		memcpy(&(fft_samples[0]), &(meter_samples[volmeter->circle_buffer_index]), first_chunk);
		if (volmeter->circle_buffer_index > 0) {
			memcpy(&(fft_samples[first_index]), &(meter_samples[0]), second_chunk);
		}
		//use the circle buffer to refer to the size of both arrays
		int buf_frames = volmeter->circle_buffer.frames;
		int fft_size = get_power_of_two(buf_frames);
		/* Window Function */
		
		for (int i = 0; i < fft_size; i++) {
			fft_samples[i] *= window_weights[(int)(i*(AUDIO_OUTPUT_FRAMES-1)/fft_size)];
		}
		
		volmeter->fft_buffer.frames = buf_frames;
		//do the processing
		audio_fft_complex_1024(volmeter->fft_buffer.data[plane_nr], fft_size);
	}

	// Clear audio channels that are not in use.
	for (; channel_nr < MAX_AUDIO_CHANNELS; channel_nr++) {
		volmeter->vol_magnitude[channel_nr] = 0.0;
		volmeter->vol_peak[channel_nr] = 0.0;
	}
}

static void volmeter_source_data_received(void *vptr, obs_source_t *source,
		const struct audio_data *data, bool muted)
{
	struct obs_volmeter *volmeter = (struct obs_volmeter *) vptr;
	float mul;
	float magnitude[MAX_AUDIO_CHANNELS];
	float peak[MAX_AUDIO_CHANNELS];
	float input_peak[MAX_AUDIO_CHANNELS];

	pthread_mutex_lock(&volmeter->mutex);

	volmeter_process_audio_data(volmeter, data);

	struct audio_data audio_buffer;
	struct audio_data fft_buffer;
	audio_buffer.frames = volmeter->circle_buffer.frames;

	//data past the mid point is symmetric (nyquist freq)
	fft_buffer.frames = (volmeter->fft_buffer.frames / 2);
	for (int channel_nr = 0; channel_nr < MAX_AUDIO_CHANNELS;
		channel_nr++) {
		audio_buffer.data[channel_nr] = bzalloc(audio_buffer.frames * sizeof(float));
		// ;3 complex results are stuffed in this one array
		fft_buffer.data[channel_nr] = bzalloc(volmeter->fft_buffer.frames * sizeof(float));
	}
	
	// Adjust magnitude/peak based on the volume level set by the user.
	// And convert to dB.
	mul = muted ? 0.0f : db_to_mul(volmeter->cur_db);
	for (int channel_nr = 0; channel_nr < MAX_AUDIO_CHANNELS;
		channel_nr++) {
		magnitude[channel_nr] = mul_to_db(
			volmeter->vol_magnitude[channel_nr] * mul);
		peak[channel_nr] = mul_to_db(
			volmeter->vol_peak[channel_nr] * mul);
		input_peak[channel_nr] = mul_to_db(
			volmeter->vol_peak[channel_nr]);
			
		//deal w/ the fft and waveform data
		int sample_nr = 0;
		int target_nr = volmeter->circle_buffer_index;

		float *meter_samples = (float *)volmeter->circle_buffer.data[channel_nr];
		float *fft_samples = (float *)volmeter->fft_buffer.data[channel_nr];
		float *target_meter_samples = (float *)audio_buffer.data[channel_nr];
		float *target_fft_samples = (float *)audio_buffer.data[channel_nr];

		for (; sample_nr < audio_buffer.frames; sample_nr++) {
			target_nr = target_nr % audio_buffer.frames;
			target_meter_samples[sample_nr] = meter_samples[target_nr] * mul;
			target_nr++;
		}

		for (sample_nr = 0; sample_nr < volmeter->fft_buffer.frames; sample_nr++) {
			target_fft_samples[sample_nr] = fft_samples[sample_nr];
		}
	}

	// The input-peak is NOT adjusted with volume, so that the user
	// can check the input-gain.

	pthread_mutex_unlock(&volmeter->mutex);

	signal_levels_updated(volmeter, magnitude, peak, input_peak);
	
	//free up data
	for (int channel_nr = 0; channel_nr < MAX_AUDIO_CHANNELS;
		channel_nr++) {
		if (audio_buffer.data[channel_nr])
			bfree(audio_buffer.data[channel_nr]);
		if (fft_buffer.data[channel_nr])
			bfree(fft_buffer.data[channel_nr]);
	}

	UNUSED_PARAMETER(source);
}

obs_fader_t *obs_fader_create(enum obs_fader_type type)
{
	struct obs_fader *fader = bzalloc(sizeof(struct obs_fader));
	if (!fader)
		return NULL;

	pthread_mutex_init_value(&fader->mutex);
	pthread_mutex_init_value(&fader->callback_mutex);
	if (pthread_mutex_init(&fader->mutex, NULL) != 0)
		goto fail;
	if (pthread_mutex_init(&fader->callback_mutex, NULL) != 0)
		goto fail;

	switch(type) {
	case OBS_FADER_CUBIC:
		fader->def_to_db = cubic_def_to_db;
		fader->db_to_def = cubic_db_to_def;
		fader->max_db    = 0.0f;
		fader->min_db    = -INFINITY;
		break;
	case OBS_FADER_IEC:
		fader->def_to_db = iec_def_to_db;
		fader->db_to_def = iec_db_to_def;
		fader->max_db    = 0.0f;
		fader->min_db    = -INFINITY;
		break;
	case OBS_FADER_LOG:
		fader->def_to_db = log_def_to_db;
		fader->db_to_def = log_db_to_def;
		fader->max_db    = 0.0f;
		fader->min_db    = -96.0f;
		break;
	default:
		goto fail;
		break;
	}
	fader->type = type;

	return fader;
fail:
	obs_fader_destroy(fader);
	return NULL;
}

void obs_fader_destroy(obs_fader_t *fader)
{
	if (!fader)
		return;

	obs_fader_detach_source(fader);
	da_free(fader->callbacks);
	pthread_mutex_destroy(&fader->callback_mutex);
	pthread_mutex_destroy(&fader->mutex);

	bfree(fader);
}

bool obs_fader_set_db(obs_fader_t *fader, const float db)
{
	if (!fader)
		return false;

	pthread_mutex_lock(&fader->mutex);

	bool clamped  = false;
	fader->cur_db = db;

	if (fader->cur_db > fader->max_db) {
		fader->cur_db = fader->max_db;
		clamped       = true;
	}
	if (fader->cur_db < fader->min_db) {
		fader->cur_db = -INFINITY;
		clamped       = true;
	}

	fader->ignore_next_signal = true;
	obs_source_t *src         = fader->source;
	const float mul           = db_to_mul(fader->cur_db);

	pthread_mutex_unlock(&fader->mutex);

	if (src)
		obs_source_set_volume(src, mul);

	return !clamped;
}

float obs_fader_get_db(obs_fader_t *fader)
{
	if (!fader)
		return 0.0f;

	pthread_mutex_lock(&fader->mutex);
	const float db = fader->cur_db;
	pthread_mutex_unlock(&fader->mutex);

	return db;
}

bool obs_fader_set_deflection(obs_fader_t *fader, const float def)
{
	if (!fader)
		return false;

	return obs_fader_set_db(fader, fader->def_to_db(def));
}

float obs_fader_get_deflection(obs_fader_t *fader)
{
	if (!fader)
		return 0.0f;

	pthread_mutex_lock(&fader->mutex);
	const float def = fader->db_to_def(fader->cur_db);
	pthread_mutex_unlock(&fader->mutex);

	return def;
}

bool obs_fader_set_mul(obs_fader_t *fader, const float mul)
{
	if (!fader)
		return false;

	return obs_fader_set_db(fader, mul_to_db(mul));
}

float obs_fader_get_mul(obs_fader_t *fader)
{
	if (!fader)
		return 0.0f;

	pthread_mutex_lock(&fader->mutex);
	const float mul = db_to_mul(fader->cur_db);
	pthread_mutex_unlock(&fader->mutex);

	return mul;
}

bool obs_fader_attach_source(obs_fader_t *fader, obs_source_t *source)
{
	signal_handler_t *sh;
	float vol;

	if (!fader || !source)
		return false;

	obs_fader_detach_source(fader);

	sh = obs_source_get_signal_handler(source);
	signal_handler_connect(sh, "volume",
			fader_source_volume_changed, fader);
	signal_handler_connect(sh, "destroy",
			fader_source_destroyed, fader);
	vol = obs_source_get_volume(source);

	pthread_mutex_lock(&fader->mutex);

	fader->source = source;
	fader->cur_db = mul_to_db(vol);

	pthread_mutex_unlock(&fader->mutex);

	return true;
}

void obs_fader_detach_source(obs_fader_t *fader)
{
	signal_handler_t *sh;
	obs_source_t *source;

	if (!fader)
		return;

	pthread_mutex_lock(&fader->mutex);
	source = fader->source;
	fader->source = NULL;
	pthread_mutex_unlock(&fader->mutex);

	if (!source)
		return;

	sh = obs_source_get_signal_handler(source);
	signal_handler_disconnect(sh, "volume",
			fader_source_volume_changed, fader);
	signal_handler_disconnect(sh, "destroy",
			fader_source_destroyed, fader);

}

void obs_fader_add_callback(obs_fader_t *fader, obs_fader_changed_t callback,
		void *param)
{
	struct fader_cb cb = {callback, param};

	if (!obs_ptr_valid(fader, "obs_fader_add_callback"))
		return;

	pthread_mutex_lock(&fader->callback_mutex);
	da_push_back(fader->callbacks, &cb);
	pthread_mutex_unlock(&fader->callback_mutex);
}

void obs_fader_remove_callback(obs_fader_t *fader, obs_fader_changed_t callback,
		void *param)
{
	struct fader_cb cb = {callback, param};

	if (!obs_ptr_valid(fader, "obs_fader_remove_callback"))
		return;

	pthread_mutex_lock(&fader->callback_mutex);
	da_erase_item(fader->callbacks, &cb);
	pthread_mutex_unlock(&fader->callback_mutex);
}

obs_volmeter_t *obs_volmeter_create(enum obs_fader_type type)
{
	struct obs_volmeter *volmeter = bzalloc(sizeof(struct obs_volmeter));
	if (!volmeter)
		return NULL;
	
	// https://en.wikipedia.org/wiki/Window_function#A_list_of_window_functions
	// Blackman-Harris window
	float const a0 = 0.35875;
	float const a1 = 0.48829;
	float const a2 = 0.14128;
	float const a3 = 0.01168;
	for (int i = 0; i < AUDIO_OUTPUT_FRAMES; i++) {
		window_weights[i] =
			a0 -
			a1*cos(2 * M_PI*i / (AUDIO_OUTPUT_FRAMES - 1)) +
			a2*cos(4 * M_PI*i / (AUDIO_OUTPUT_FRAMES - 1)) -
			a3*cos(6 * M_PI*i / (AUDIO_OUTPUT_FRAMES - 1));
	}

	pthread_mutex_init_value(&volmeter->mutex);
	pthread_mutex_init_value(&volmeter->callback_mutex);
	if (pthread_mutex_init(&volmeter->mutex, NULL) != 0)
		goto fail;
	if (pthread_mutex_init(&volmeter->callback_mutex, NULL) != 0)
		goto fail;

	volmeter->type = type;

	obs_volmeter_set_update_interval(volmeter, 50);

	return volmeter;
fail:
	obs_volmeter_destroy(volmeter);
	return NULL;
}

void obs_volmeter_destroy(obs_volmeter_t *volmeter)
{
	if (!volmeter)
		return;

	obs_volmeter_detach_source(volmeter);
	da_free(volmeter->callbacks);
	pthread_mutex_destroy(&volmeter->callback_mutex);
	pthread_mutex_destroy(&volmeter->mutex);

	bfree(volmeter);
}

bool obs_volmeter_attach_source(obs_volmeter_t *volmeter, obs_source_t *source)
{
	signal_handler_t *sh;
	float vol;

	if (!volmeter || !source)
		return false;

	obs_volmeter_detach_source(volmeter);

	sh = obs_source_get_signal_handler(source);
	signal_handler_connect(sh, "volume",
			volmeter_source_volume_changed, volmeter);
	signal_handler_connect(sh, "destroy",
			volmeter_source_destroyed, volmeter);
	obs_source_add_audio_capture_callback(source,
			volmeter_source_data_received, volmeter);
	vol = obs_source_get_volume(source);

	pthread_mutex_lock(&volmeter->mutex);

	volmeter->source = source;
	volmeter->cur_db = mul_to_db(vol);

	pthread_mutex_unlock(&volmeter->mutex);

	return true;
}

void obs_volmeter_detach_source(obs_volmeter_t *volmeter)
{
	signal_handler_t *sh;
	obs_source_t *source;

	if (!volmeter)
		return;

	pthread_mutex_lock(&volmeter->mutex);
	source = volmeter->source;
	volmeter->source = NULL;
	pthread_mutex_unlock(&volmeter->mutex);

	if (!source)
		return;

	sh = obs_source_get_signal_handler(source);
	signal_handler_disconnect(sh, "volume",
			volmeter_source_volume_changed, volmeter);
	signal_handler_disconnect(sh, "destroy",
			volmeter_source_destroyed, volmeter);
	obs_source_remove_audio_capture_callback(source,
			volmeter_source_data_received, volmeter);
}

void obs_volmeter_set_update_interval(obs_volmeter_t *volmeter,
		const unsigned int ms)
{
	if (!volmeter || !ms)
		return;

	pthread_mutex_lock(&volmeter->mutex);
	volmeter->update_ms = ms;
	pthread_mutex_unlock(&volmeter->mutex);
}

unsigned int obs_volmeter_get_update_interval(obs_volmeter_t *volmeter)
{
	if (!volmeter)
		return 0;

	pthread_mutex_lock(&volmeter->mutex);
	const unsigned int interval = volmeter->update_ms;
	pthread_mutex_unlock(&volmeter->mutex);

	return interval;
}

int obs_volmeter_get_nr_channels(obs_volmeter_t *volmeter)
{
	int source_nr_audio_channels;
	int obs_nr_audio_channels;

	if (volmeter->source) {
		source_nr_audio_channels = get_audio_channels(
			volmeter->source->sample_info.speakers);
	} else {
		source_nr_audio_channels = 1;
	}

	struct obs_audio_info audio_info;
	if (obs_get_audio_info(&audio_info)) {
		obs_nr_audio_channels = get_audio_channels(audio_info.speakers);
	} else {
		obs_nr_audio_channels = 2;
	}

	return CLAMP(source_nr_audio_channels, 1, obs_nr_audio_channels);
}

void obs_volmeter_add_callback(obs_volmeter_t *volmeter,
		obs_volmeter_updated_t callback, void *param)
{
	struct meter_cb cb = {callback, param};

	if (!obs_ptr_valid(volmeter, "obs_volmeter_add_callback"))
		return;

	pthread_mutex_lock(&volmeter->callback_mutex);
	da_push_back(volmeter->callbacks, &cb);
	pthread_mutex_unlock(&volmeter->callback_mutex);
}

void obs_volmeter_remove_callback(obs_volmeter_t *volmeter,
		obs_volmeter_updated_t callback, void *param)
{
	struct meter_cb cb = {callback, param};

	if (!obs_ptr_valid(volmeter, "obs_volmeter_remove_callback"))
		return;

	pthread_mutex_lock(&volmeter->callback_mutex);
	da_erase_item(volmeter->callbacks, &cb);
	pthread_mutex_unlock(&volmeter->callback_mutex);
}

