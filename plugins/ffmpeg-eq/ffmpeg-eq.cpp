#include "obs-module.h"
OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("ffmpeg_eq", "en-US")

#include "media-io/audio-math.h"
#include "fft.h"
#include <util/platform.h>
#include <util/threading.h>
#include <vector>
#define blog(level, msg, ...) blog(level, "ffmpeg-eq: " msg, ##__VA_ARGS__)

struct parametric {
	float db;
	float f;
	float q;
};

template<class DataType>
DataType clamp(DataType x, DataType min, DataType max)
{
	if (x < min)
		return min;
	else if (x > max)
		return max;
	return x;
}

template<class DataType>
inline void complexify(DataType *x, int n)
{
	return;
	int hn = n / 2;
	int i2 = 0;
	/*
	for (size_t i = n - 1; i >= hn; i--)
		x[i] = x[i2++];
		*/
	/*
	for (size_t i = n - 1; i >= hn; i--)
		x[i] = 0;
		*/
	/*
	for (size_t i = 0; i < hn; i++)
		x[2 * i + 1] = 0;
		*/
}

class PThreadMutex {
	bool            _mutexCreated;
	pthread_mutex_t _mutex;

public:
	PThreadMutex(int PTHREAD_MUTEX_TYPE = PTHREAD_MUTEX_RECURSIVE)
	{
		pthread_mutexattr_t attr;
		if (pthread_mutexattr_init(&attr) != 0) {
			_mutexCreated = false;
			return;
		}
		if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_TYPE) != 0) {
			_mutexCreated = false;
			return;
		}
		if (pthread_mutex_init(&_mutex, &attr) != 0)
			_mutexCreated = false;
		else
			_mutexCreated = true;
	}

	~PThreadMutex()
	{
		if (_mutexCreated)
			pthread_mutex_destroy(&_mutex);
	}

	int trylock()
	{
		if (_mutexCreated)
			return pthread_mutex_trylock(&_mutex);
		return -1;
	}

	void lock()
	{
		if (_mutexCreated)
			pthread_mutex_lock(&_mutex);
	}

	void unlock()
	{
		if (_mutexCreated)
			pthread_mutex_unlock(&_mutex);
	}
};

class ParametricEq {
private:
	obs_source_t *_context = nullptr;
	obs_data_t *_settings = nullptr;
	float *buffer[MAX_AUDIO_CHANNELS];
	float *out_buffer[MAX_AUDIO_CHANNELS];
	size_t buffer_size;
	size_t buffer_bytes;
	size_t frames = 0;
	size_t sample_rate;
	RDFTContext *rdft = nullptr;
	RDFTContext *irdft = nullptr;
	PThreadMutex *audiomutex = nullptr;
public:
	std::vector<parametric> _bands;
	std::vector<float> _mul;

	ParametricEq(obs_data_t *settings, obs_source_t *source)
	{
		_settings = settings;
		_context = source;
		audiomutex = new PThreadMutex();

		struct obs_audio_info aoi;
		obs_get_audio_info(&aoi);
		sample_rate = aoi.samples_per_sec;

		parametric s;
		s.f = 200.0f;
		s.db = 6.0f;
		s.q = 100.0f;
		_bands.push_back(s);

		/* Create buffers */
		_mul.reserve(AUDIO_OUTPUT_FRAMES * 8);
		buffer_size = AUDIO_OUTPUT_FRAMES;
		buffer_bytes = buffer_size * sizeof(float);
		for (size_t c = 0; c < MAX_AUDIO_CHANNELS; c++) {
			buffer[c] = (float*)bzalloc(buffer_bytes);
			out_buffer[c] = (float*)bzalloc(buffer_bytes * 8);
		}

		for (size_t i = 0; i < buffer_size; i++)
			_mul.emplace_back(1.0f);

		int l = (int)ceil(log2(AUDIO_OUTPUT_FRAMES * 8));
		rdft = av_init_rdft(l, DFT_R2C);
		irdft = av_init_rdft(l, IDFT_C2R);

		updateMul();
	}

	void resize_mul(size_t samples)
	{
		if (samples != _mul.size()) {
			if (rdft)
				av_end_rdft(rdft);
			if (irdft)
				av_end_rdft(irdft);
			int l = (int)ceil(log2(samples));
			rdft = av_init_rdft(l, DFT_R2C);
			irdft = av_init_rdft(l, IDFT_C2R);
		}
		_mul.reserve(samples);
		for (size_t i = _mul.size(); i < samples; i++)
			_mul.emplace_back(1.0f);
	}

	void reset_mul()
	{
		for (size_t i = 0; i < _mul.size(); i++)
			_mul[i] = 1.0f;
	}

	void resize_input_buffer(size_t samples)
	{
		buffer_size = samples;
		buffer_bytes = buffer_size * sizeof(float);
		for (size_t c = 0; c < MAX_AUDIO_CHANNELS; c++) {
			buffer[c] = (float*)brealloc(buffer[c], buffer_bytes);
		}
	}

	void resize_output_buffer(size_t samples)
	{
		resize_mul(samples);
		for (size_t c = 0; c < MAX_AUDIO_CHANNELS; c++) {
			out_buffer[c] = (float*)brealloc(out_buffer[c],
				_mul.size() * sizeof(float));
		}
	}

	~ParametricEq()
	{
		for (size_t c = 0; c < MAX_AUDIO_CHANNELS; c++) {
			bfree(buffer[c]);
			bfree(out_buffer[c]);
		}
		av_end_rdft(rdft);
		av_end_rdft(irdft);
	}

	static void *Create(obs_data_t *settings, obs_source_t *source)
	{
		return new ParametricEq(settings, source);
	}

	static void Destroy(void *vptr)
	{
		delete static_cast<ParametricEq*>(vptr);
	}

	void shift_buffer_right(size_t samples)
	{
		if (samples >= buffer_size) {
			for (size_t i = 0; i < MAX_AUDIO_CHANNELS; i++)
				memset(buffer[i], 0, buffer_bytes);
			return;
		}
		for (size_t i = 0; i < MAX_AUDIO_CHANNELS; i++) {
			for (size_t f = buffer_size - 1; f > samples; f--)
				buffer[i][f] = buffer[i][f-samples];
		}
	}

	void shift_buffer_left(size_t samples)
	{
		if (samples >= buffer_size) {
			for (size_t i = 0; i < MAX_AUDIO_CHANNELS; i++)
				memset(buffer[i], 0, buffer_bytes);
			return;
		}
		size_t end = buffer_size - samples;
		for (size_t i = 0; i < MAX_AUDIO_CHANNELS; i++) {
			for (size_t f = 0; f < end; f++)
				buffer[i][f] = buffer[i][f + samples];
		}
	}

	void append_audio(obs_audio_data *audio)
	{
		if (frames + audio->frames > buffer_size)
			resize_input_buffer(frames + audio->frames);

		size_t frames_bytes = audio->frames * sizeof(float);
		//audiomutex->lock();
		for (size_t i = 0; i < MAX_AUDIO_CHANNELS; i++) {
			if (audio->data[i])
				memcpy(&buffer[i][frames], audio->data[i],
						frames_bytes);
			else
				memset(&buffer[i][frames], 0, frames_bytes);
		}
		frames += audio->frames;
		//audiomutex->unlock();
	}

	size_t band(float freq)
	{
		float spacing = (float)_mul.size() / (float)sample_rate;
		return clamp((size_t)(freq / spacing), (size_t)0, _mul.size()-1);
	}

	float freq(size_t band)
	{
		float spacing = (float)_mul.size() / (float)sample_rate;
		return clamp(band, (size_t)0 , _mul.size()-1) * spacing;
	}

	void updateMul()
	{
		size_t samples = _mul.size();
		float spacing = (float)_mul.size() / (float)sample_rate;
		reset_mul();
		for (size_t i = 0; i < _bands.size(); i++) {
			parametric d = _bands[i];
			float lo_freq = d.f - d.q;
			float hi_freq = d.f + d.q;
			lo_freq = clamp(lo_freq, 0.0f, (float)sample_rate);
			hi_freq = clamp(hi_freq, 0.0f, (float)sample_rate);
			float mul = db_to_mul(d.db);
			size_t lo_band = band(lo_freq);
			size_t hi_band = band(hi_freq);
			size_t center_band = band(d.f);
			float diff = 2 * d.q;
			if (d.q) {
				for (size_t j = lo_band; j <= hi_band; j++) {
					float f = freq(j);
					float dist = (float)f - d.f;
					float offset = clamp((dist / d.q), -1.0f, 1.0f);
					_mul[j] *= (cos((offset * M_PI)) + 1.0f) * mul / 2.0f;
				}
			} else {
				float f = freq(center_band);
				float dist = (float)f - d.f;
				float offset = clamp((dist / d.q), -1.0f, 1.0f);
				_mul[center_band] *= (cos((offset * M_PI)) + 1.0f) * mul / 2.0f;
			}
		}
		complexify(_mul.data(), _mul.size());
	}

	void update(obs_data_t *settings)
	{
		_settings = settings;
		updateMul();
	}

	static void Update(void *vptr, obs_data_t *settings)
	{
		ParametricEq *eq = static_cast<ParametricEq*>(vptr);
		if(eq)
			eq->update(settings);
	}

	static obs_properties_t *Properties(void *vptr)
	{
		ParametricEq *eq = static_cast<ParametricEq*>(vptr);
		obs_properties_t *props = obs_properties_create();
		return props;
	}

	struct obs_audio_data *process_audio(struct obs_audio_data *audio)
	{
		append_audio(audio);
		if (frames < _mul.size())
			return nullptr;

		audio->frames = _mul.size();
		for (size_t c = 0; c < MAX_AUDIO_CHANNELS; c++) {
			float *a = (float*)audio->data[c];
			if (a) {
				float *ob = (float*)out_buffer[c];
				float *ib = (float*)buffer[c];

				memcpy(ob, ib, _mul.size() * sizeof(float));
				//av_calc_rdft(rdft, ob);
				audio_fft_complex(ob, _mul.size());

				/*
				buf[0] *= kernel_buf[0];
				buf[1] *= kernel_buf[s->rdft_len / 2];
				for (k = 1; k < s->rdft_len / 2; k++) {
					buf[2 * k] *= kernel_buf[k];
					buf[2 * k + 1] *= kernel_buf[k];
				}
				*/
				for (size_t f = 2; f < _mul.size(); f+=2) {
					float re = ob[f] - ob[f + 1];
					float im = ob[f] + ob[f + 1];
					ob[f] = re;
					ob[f + 1] = im;
				}

				//for (size_t f = 0; f < _mul.size(); f++)
				//	ob[f] *= _mul[f];
					
				//av_calc_rdft(irdft, ob);
				audio_ifft_complex(ob, _mul.size());
				audio->data[c] = (uint8_t*)bmemdup(ob, _mul.size() * sizeof(float));
			}
		}
		frames -= _mul.size();
		shift_buffer_left(_mul.size());
		return audio;
	}

	static const char *Name(void *unused)
	{
		UNUSED_PARAMETER(unused);
		return obs_module_text("EQ");
	}

	static struct obs_audio_data *Filter(void *data,
			struct obs_audio_data *audio)
	{
		ParametricEq *eq = static_cast<ParametricEq*>(data);
		return eq->process_audio(audio);
	}

	static void Defaults(obs_data_t *settings)
	{

	}
};

bool obs_module_load()
{
	struct obs_source_info eq = { 0 };
	eq.id = "ffmpeg_eq";
	eq.type = OBS_SOURCE_TYPE_FILTER;
	eq.output_flags = OBS_SOURCE_AUDIO;
	eq.get_name = ParametricEq::Name;
	eq.create = ParametricEq::Create;
	eq.destroy = ParametricEq::Destroy;
	eq.update = ParametricEq::Update;
	eq.filter_audio = ParametricEq::Filter;
	eq.get_defaults = ParametricEq::Defaults;
	eq.get_properties = ParametricEq::Properties;
	obs_register_source(&eq);
	return true;
}

void obs_module_unload()
{

}
