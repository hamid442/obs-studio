#include <obs-module.h>
#include <QWidget>
#include <QLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QDragMoveEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <media-io/audio-math.h>
#include <math.h>
#include "QGainWidget.h"

#define do_log(level, format, ...) \
	blog(level, "[gain filter: '%s'] " format, \
			obs_source_get_name(gf->context), ##__VA_ARGS__)

#define warn(format, ...)  do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...)  do_log(LOG_INFO,    format, ##__VA_ARGS__)

#define S_GAIN_DB                      "db"

#define MT_ obs_module_text
#define TEXT_GAIN_DB                   MT_("Gain.GainDB")

#define CLIP_FLASH_DURATION_MS 1000

struct gain_data {
	obs_source_t *context;
	size_t channels;
	float multiple;
	pthread_mutex_t mutex;
	float peak[MAX_AUDIO_CHANNELS];
	float input_peak[MAX_AUDIO_CHANNELS];
	float mag[MAX_AUDIO_CHANNELS];
	float input_mag[MAX_AUDIO_CHANNELS];
};

struct audio_peak_data {
	size_t channels;
	float multiple;
	pthread_mutex_t mutex;
	float peak[MAX_AUDIO_CHANNELS];
	float input_peak[MAX_AUDIO_CHANNELS];
	float mag[MAX_AUDIO_CHANNELS];
	float input_mag[MAX_AUDIO_CHANNELS];
};

void QGainWidget::updateDb(double db) {
	_db = db;
	_mul = db_to_mul(_db);
	double diffDb = abs(_minDb - _maxDb);
	double relDb = _db - _minDb;
	_scale = relDb / diffDb;

	obs_data_t *settings = obs_source_get_settings(_source);
	obs_data_set_double(settings, "db", (double)_db);
	obs_source_update(_source, settings);
	obs_data_release(settings);
	obs_source_update_properties(_source);
}

void QGainWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
	slider->setDoubleVal(0.0);
}

QGainWidget::QGainWidget(QWidget* parent, Qt::WindowFlags f,
	obs_source_t *source) : QWidget(parent, f)
{
	redrawTimer = nullptr;
	_minDb = -30.0;
	_maxDb = 30.0;
	_source = source;
	_parent = obs_filter_get_parent(_source);

	obs_data_t *settings = obs_source_get_settings(_source);
	_db = (double)obs_data_get_double(settings, "db");
	_mul = db_to_mul(_db);
	/*_minDb, _db ,_maxDb*/
	double diffDb = abs(_minDb - _maxDb);
	double relDb = _db - _minDb;
	_scale = relDb / diffDb;
	obs_data_release(settings);

	beforeMeter = new OBSAudioMeter(parent, f, source);
	afterMeter = new OBSAudioMeter(parent, f, source);
	slider = new DoubleSlider();

	slider->setDoubleConstraints(_minDb, _maxDb, 0.1, _db);

	beforeMeter->setLayout(vertical);
	afterMeter->setLayout(vertical);
	if (vertical) {
		QHBoxLayout *layout = new QHBoxLayout();
		this->setLayout(layout);
		slider->setOrientation(Qt::Vertical);
		beforeMeter->setTickOptions(OBSAudioMeter::right, true);
		afterMeter->setTickOptions(OBSAudioMeter::left, false);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(0);
		layout->addWidget(beforeMeter);
		layout->addWidget(slider);
		layout->addWidget(afterMeter);
		layout->addStretch();
	} else {
		QVBoxLayout *layout = new QVBoxLayout();
		this->setLayout(layout);
		slider->setOrientation(Qt::Horizontal);
		beforeMeter->setTickOptions(OBSAudioMeter::bottom, true);
		afterMeter->setTickOptions(OBSAudioMeter::top, false);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(0);
		layout->addWidget(beforeMeter);
		layout->addWidget(slider);
		layout->addWidget(afterMeter);
		layout->addStretch();
	}

	connect(slider, SIGNAL(doubleValChanged(double)),
		this, SLOT(updateDb(double)));
}

extern "C" {

	const char *gain_name(void *unused)
	{
		UNUSED_PARAMETER(unused);
		return obs_module_text("Gain");
	}

	void gain_destroy(void *data)
	{
		struct gain_data *gf = static_cast<struct gain_data*>(data);
		bfree(gf);
	}

	void gain_update(void *data, obs_data_t *s)
	{
		struct gain_data *gf = static_cast<struct gain_data*>(data);
		double val = obs_data_get_double(s, S_GAIN_DB);
		gf->channels = audio_output_get_channels(obs_get_audio());
		gf->multiple = db_to_mul((float)val);
	}

	void *gain_create(obs_data_t *settings, obs_source_t *filter)
	{
		struct gain_data *gf = static_cast<struct gain_data*>(
			bzalloc(sizeof(*gf)));

		gf->context = filter;
		if (pthread_mutex_init(&gf->mutex, nullptr) != 0) {
			bfree(gf);
			return nullptr;
		}
		gain_update(gf, settings);
		return gf;
	}

	struct obs_audio_data *gain_filter_audio(void *data,
		struct obs_audio_data *audio)
	{
		struct gain_data *gf = static_cast<struct gain_data*>(data);
		const size_t channels = gf->channels;
		float **adata = (float**)audio->data;
		const float multiple = gf->multiple;

		for (size_t c = 0; c < channels; c++) {
			if (audio->data[c]) {
				float input_sum = 0.0;
				float input_peak = 0.0;
				for (size_t i = 0; i < audio->frames; i++) {
					float sample = adata[c][i];
					input_sum += sample * sample;
					float abs_sample = std::abs(sample);
					if (abs_sample > input_peak)
						input_peak = abs_sample;
				}
				/* apply gain */
				for (size_t i = 0; i < audio->frames; i++) {
					adata[c][i] *= multiple;
				}

				float sum = 0.0;
				for (size_t i = 0; i < audio->frames; i++) {
					float sample = adata[c][i];
					sum += sample * sample;
				}
	
				if (pthread_mutex_trylock(&gf->mutex) == 0) {
					gf->input_peak[c] = input_peak;
					gf->peak[c] = input_peak * multiple;
					gf->input_mag[c] = sqrtf(input_sum /
						audio->frames);
					gf->mag[c] = sqrtf(sum / audio->frames);

					pthread_mutex_unlock(&gf->mutex);
				}
			} else {
				if (pthread_mutex_trylock(&gf->mutex) == 0) {
					gf->input_peak[c] = 0.0;
					gf->peak[c] = 0.0;
					gf->mag[c] = 0.0;
					gf->input_mag[c] = 0.0;

					pthread_mutex_unlock(&gf->mutex);
				}
			}
		}

		return audio;
	}

	void gain_defaults(obs_data_t *s)
	{
		obs_data_set_default_double(s, S_GAIN_DB, 0.0f);
	}

	obs_properties_t *gain_properties(void *data)
	{
		struct gain_data *gf = static_cast<struct gain_data*>(data);
		obs_properties_t *ppts = obs_properties_create();

		obs_properties_add_float_slider(ppts, S_GAIN_DB, TEXT_GAIN_DB,
			-30.0, 30.0, 0.1);

		obs_properties_set_param(ppts, &gf->channels, nullptr);
		return ppts;
	}

	static void getSample(OBSAudioMeter *meter)
	{
		if (!meter)
			return;
		const obs_source_t *source = meter->getSource();
		obs_properties_t *props = obs_source_properties(source);
		struct audio_peak_data* data = static_cast<struct audio_peak_data*>(
			obs_properties_get_param(props));

		if (data) {
			if (pthread_mutex_trylock(&data->mutex) == 0) {
				meter->setChannels(data->channels);
				meter->setLevels(data->mag, data->peak,
					data->input_peak);

				pthread_mutex_unlock(&data->mutex);
			}
		} else {
			meter->resetLevels();
		}
		obs_properties_destroy(props);
	}

	static void getAfterSample(OBSAudioMeter *meter)
	{
		if (!meter)
			return;
		const obs_source_t *source = meter->getSource();
		obs_properties_t *props = obs_source_properties(source);
		struct audio_peak_data* data = static_cast<struct audio_peak_data*>(
			obs_properties_get_param(props));

		if (data) {
			if (pthread_mutex_trylock(&data->mutex) == 0) {
				meter->setChannels(data->channels);
				meter->setLevels(data->mag, data->peak,
					data->peak);

				pthread_mutex_unlock(&data->mutex);
			}
		} else {
			meter->resetLevels();
		}
		obs_properties_destroy(props);
	}

	static void getBeforeSample(OBSAudioMeter *meter)
	{
		if (!meter)
			return;
		const obs_source_t *source = meter->getSource();
		obs_properties_t *props = obs_source_properties(source);
		struct audio_peak_data* data = static_cast<struct audio_peak_data*>(
			obs_properties_get_param(props));

		if (data) {
			if (pthread_mutex_trylock(&data->mutex) == 0) {
				meter->setChannels(data->channels);
				meter->setLevels(data->input_mag,
					data->input_peak, data->input_peak);

				pthread_mutex_unlock(&data->mutex);
			}
		} else {
			meter->resetLevels();
		}
		obs_properties_destroy(props);
	}

	void *gain_ui(void *source, void *parent)
	{
		QWidget *parentWidget = static_cast<QWidget*>(parent);
		QGainWidget *meter = new QGainWidget(parentWidget,
			Qt::WindowFlags(), static_cast<obs_source_t*>(source));
			
		meter->setBeforeCallback(getBeforeSample);
		meter->setAfterCallback(getAfterSample);
		
		return meter;
	}

}
