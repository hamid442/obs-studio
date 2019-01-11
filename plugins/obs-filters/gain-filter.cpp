#include <obs-module.h>
#include <QWidget>
#include <QLayout>
#include <QVBoxLayout>
//#include <QSpinbox>
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

inline void QGainWidget::calculateBallisticsForChannel(int channelNr,
	uint64_t ts, qreal timeSinceLastRedraw)
{
	if (currentPeak[channelNr] >= displayPeak[channelNr] ||
		isnan(displayPeak[channelNr])) {
		// Attack of peak is immediate.
		displayPeak[channelNr] = currentPeak[channelNr];
	} else {
		// Decay of peak is 40 dB / 1.7 seconds for Fast Profile
		// 20 dB / 1.7 seconds for Medium Profile (Type I PPM)
		// 24 dB / 2.8 seconds for Slow Profile (Type II PPM)
		float decay = float(peakDecayRate * timeSinceLastRedraw);
		displayPeak[channelNr] = CLAMP(displayPeak[channelNr] - decay,
			currentPeak[channelNr], 0.0);
	}

	if (currentInputPeak[channelNr] >= displayInputPeak[channelNr] ||
		isnan(displayInputPeak[channelNr])) {
		// Attack of peak is immediate.
		displayInputPeak[channelNr] = currentInputPeak[channelNr];
	} else {
		// Decay of peak is 40 dB / 1.7 seconds for Fast Profile
		// 20 dB / 1.7 seconds for Medium Profile (Type I PPM)
		// 24 dB / 2.8 seconds for Slow Profile (Type II PPM)
		float decay = float(peakDecayRate * timeSinceLastRedraw);
		displayInputPeak[channelNr] = CLAMP(displayInputPeak[channelNr] - decay,
			currentInputPeak[channelNr], 0.0);
	}
	
	if (currentPeak[channelNr] >= displayPeakHold[channelNr] ||
		!isfinite(displayPeakHold[channelNr])) {
		// Attack of peak-hold is immediate, but keep track
		// when it was last updated.
		displayPeakHold[channelNr] = currentPeak[channelNr];
		displayPeakHoldLastUpdateTime[channelNr] = ts;
	} else {
		// The peak and hold falls back to peak
		// after 20 seconds.
		qreal timeSinceLastPeak = (uint64_t)(ts -
			displayPeakHoldLastUpdateTime[channelNr]) * 0.000000001;
		if (timeSinceLastPeak > peakHoldDuration) {
			displayPeakHold[channelNr] = currentPeak[channelNr];
			displayPeakHoldLastUpdateTime[channelNr] = ts;
		}
	}
	
	if (currentInputPeak[channelNr] >= displayInputPeakHold[channelNr] ||
		!isfinite(displayInputPeakHold[channelNr])) {
		// Attack of peak-hold is immediate, but keep track
		// when it was last updated.
		displayInputPeakHold[channelNr] = currentInputPeak[channelNr];
		displayInputPeakHoldLastUpdateTime[channelNr] = ts;
	} else {
		// The peak and hold falls back to peak after 1 second.
		qreal timeSinceLastPeak = (uint64_t)(ts -
			displayInputPeakHoldLastUpdateTime[channelNr]) *
			0.000000001;
		if (timeSinceLastPeak > inputPeakHoldDuration) {
			displayInputPeakHold[channelNr] =
				currentInputPeak[channelNr];
			displayInputPeakHoldLastUpdateTime[channelNr] =
				ts;
		}
	}
	
	if (!isfinite(displayMagnitude[channelNr])) {
		// The statements in the else-leg do not work with
		// NaN and infinite displayMagnitude.
		displayMagnitude[channelNr] = currentMagnitude[channelNr];
	} else {
		// A VU meter will integrate to the new value to 99% in 300 ms.
		// The calculation here is very simplified and is more accurate
		// with higher frame-rate.
		float attack = float((currentMagnitude[channelNr] -
			displayMagnitude[channelNr]) *
			(timeSinceLastRedraw /
				magnitudeIntegrationTime) * 0.99);
		displayMagnitude[channelNr] = CLAMP(displayMagnitude[channelNr]
			+ attack, (float)minimumLevel, 0.0);
	}
}

void QGainWidget::calculateBallistics(uint64_t ts, qreal timeSinceLastRedraw)
{
	QMutexLocker locker(&dataMutex);

	for (int channelNr = 0; channelNr < MAX_AUDIO_CHANNELS; channelNr++)
		calculateBallisticsForChannel(channelNr, ts,
			timeSinceLastRedraw);
}

void QGainWidget::paintInputMeter(QPainter &painter, int x, int y, int width,
	int height, float peakHold)
{
	QMutexLocker locker(&dataMutex);
	QColor color;

	if (peakHold < minimumLevel)
		color = backgroundNominalColor;
	else if (peakHold < warningLevel)
		color = foregroundNominalColor;
	else if (peakHold < errorLevel)
		color = foregroundWarningColor;
	else if (peakHold <= 0.0f)
		color = foregroundErrorColor;
	else
		color = clipColor;

	painter.fillRect(x, y, width, height, color);
}

void QGainWidget::paintHMeter(QPainter &painter, int x, int y, int width,
	int height, float magnitude, float peak, float inputPeak, float peakHold, float inputPeakHold = -M_INFINITE)
{
	qreal scale = width / minimumLevel;

	QMutexLocker locker(&dataMutex);
	int minimumPosition = x + 0;
	int maximumPosition = x + width;
	int magnitudePosition = int(x + width - (magnitude * scale));
	int peakPosition = int(x + width - (peak * scale));
	int inputPeakPosition = int(x + width - (inputPeak * scale));
	int peakHoldPosition = int(x + width - (peakHold * scale));
	int inputPeakHoldPosition = int(x + width - (inputPeakHold * scale));
	int warningPosition = int(x + width - (warningLevel * scale));
	int errorPosition = int(x + width - (errorLevel * scale));

	int nominalLength = warningPosition - minimumPosition;
	int warningLength = errorPosition - warningPosition;
	int errorLength = maximumPosition - errorPosition;
	locker.unlock();

	auto drawMeter = [&painter, &minimumPosition, &warningPosition,
		&errorPosition, &maximumPosition, &nominalLength, &warningLength, &errorLength,
		&x, &y, &height](int peak, int peakHold, const QColor &foregroundNominalColor,
			const QColor &foregroundWarningColor, const QColor &foregroundErrorColor) {
		if (peak < minimumPosition) {

		} else if (peak < warningPosition) {
			painter.fillRect(minimumPosition, y, peak -
				minimumPosition, height,
				foregroundNominalColor);
		} else if (peak < errorPosition) {
			painter.fillRect(minimumPosition, y, nominalLength, height,
				foregroundNominalColor);
			painter.fillRect(warningPosition, y,
				peak - warningPosition, height,
				foregroundWarningColor);
		} else if (peak < maximumPosition) {
			painter.fillRect(minimumPosition, y, nominalLength, height,
				foregroundNominalColor);
			painter.fillRect(warningPosition, y, warningLength, height,
				foregroundWarningColor);
			painter.fillRect(errorPosition, y, peak - errorPosition,
				height, foregroundErrorColor);
		} else {
			painter.fillRect(minimumPosition, y, nominalLength, height,
				foregroundNominalColor);
			painter.fillRect(warningPosition, y, warningLength, height,
				foregroundWarningColor);
			painter.fillRect(errorPosition, y, errorLength, height,
				foregroundErrorColor);
		}
		/* Draw Peak Hold */
		if (peakHold - 3 < minimumPosition)
			;// Peak-hold below minimum, no drawing.
		else if (peakHold < warningPosition)
			painter.fillRect(peakHold - 3, y, 3, height,
				foregroundNominalColor);
		else if (peakHold < errorPosition)
			painter.fillRect(peakHold - 3, y, 3, height,
				foregroundWarningColor);
		else if (peakHold < maximumPosition)
			painter.fillRect(peakHold - 3, y, 3, height,
				foregroundErrorColor);
		else
			painter.fillRect(maximumPosition - 7, y, 7, height,
				foregroundErrorColor);
	};
	/*Draw Background*/
	painter.fillRect(minimumPosition, y, nominalLength, height,
		backgroundNominalColor);
	painter.fillRect(warningPosition, y, warningLength, height,
		backgroundWarningColor);
	painter.fillRect(errorPosition, y, errorLength, height,
		backgroundErrorColor);

	/*Draw Meter*/
	if (peakPosition < inputPeakPosition)
		drawMeter(inputPeakPosition, inputPeakHoldPosition, diffNominalColor, diffWarningColor, diffErrorColor);

	drawMeter(peakPosition, peakHoldPosition, foregroundNominalColor, foregroundWarningColor, foregroundErrorColor);

	if (peakPosition > inputPeakPosition)
		drawMeter(inputPeakPosition, inputPeakHoldPosition, diffNominalColor, diffWarningColor, diffErrorColor);

	/*Draw Magnitude*/
	if (magnitudePosition - 3 >= minimumPosition)
		painter.fillRect(magnitudePosition - 3, y, 3, height,
			magnitudeColor);
}

void QGainWidget::paintVMeter(QPainter &painter, int x, int y, int width,
	int height, float magnitude, float peak, float inputPeak, float peakHold, float inputPeakHold = -M_INFINITE)
{
	qreal scale = height / minimumLevel;

	QMutexLocker locker(&dataMutex);
	int minimumPosition = y + 0;
	int maximumPosition = y + height;
	int magnitudePosition = int(y + height - (magnitude * scale));
	int peakPosition = int(y + height - (peak * scale));
	int inputPeakPosition = int(y + height - (inputPeak * scale));
	int peakHoldPosition = int(y + height - (peakHold * scale));
	int inputPeakHoldPosition = int(y + height - (inputPeakHold * scale));
	int warningPosition = int(y + height - (warningLevel * scale));
	int errorPosition = int(y + height - (errorLevel * scale));

	int nominalLength = warningPosition - minimumPosition;
	int warningLength = errorPosition - warningPosition;
	int errorLength = maximumPosition - errorPosition;
	locker.unlock();

	auto drawMeter = [&painter, &minimumPosition, &warningPosition,
		&errorPosition, &maximumPosition, &nominalLength, &warningLength, &errorLength,
		&x, &y, &width](int peak, int peakHold, const QColor &foregroundNominalColor,
			const QColor &foregroundWarningColor, const QColor &foregroundErrorColor) {
		if (peak < minimumPosition) {

		} else if (peak < warningPosition) {
			painter.fillRect(x, minimumPosition, width, peak -
				minimumPosition, foregroundNominalColor);
		} else if (peak < errorPosition) {
			painter.fillRect(x, minimumPosition, width, nominalLength,
				foregroundNominalColor);
			painter.fillRect(x, warningPosition, width, peak -
				warningPosition, foregroundWarningColor);
		} else if (peak < maximumPosition) {
			painter.fillRect(x, minimumPosition, width, nominalLength,
				foregroundNominalColor);
			painter.fillRect(x, warningPosition, width, warningLength,
				foregroundWarningColor);
			painter.fillRect(x, errorPosition, width, peak -
				errorPosition, foregroundErrorColor);
		} else {
			painter.fillRect(x, minimumPosition, width, nominalLength,
				foregroundNominalColor);
			painter.fillRect(x, warningPosition, width, warningLength,
				foregroundWarningColor);
			painter.fillRect(x, errorPosition, width, errorLength,
				foregroundErrorColor);
		}

		if (peakHold - 3 < minimumPosition)
			;// Peak-hold below minimum, no drawing.
		else if (peakHold < warningPosition)
			painter.fillRect(x, peakHold - 3, width, 3,
				foregroundNominalColor);
		else if (peakHold < errorPosition)
			painter.fillRect(x, peakHold - 3, width, 3,
				foregroundWarningColor);
		else if (peakHold < maximumPosition)
			painter.fillRect(x, peakHold - 3, width, 3,
				foregroundErrorColor);
		else
			painter.fillRect(x, maximumPosition - 3, width, 3,
				foregroundErrorColor);
	};
	/*Draw Background*/
	painter.fillRect(x, minimumPosition, width, nominalLength,
		backgroundNominalColor);
	painter.fillRect(x, warningPosition, width, warningLength,
		backgroundWarningColor);
	painter.fillRect(x, errorPosition, width, errorLength,
		backgroundErrorColor);

	/*Draw Meter*/
	if (peakPosition < inputPeakPosition)
		drawMeter(inputPeakPosition, inputPeakHoldPosition, diffNominalColor, diffWarningColor, diffErrorColor);

	drawMeter(peakPosition, peakHoldPosition, foregroundNominalColor, foregroundWarningColor, foregroundErrorColor);

	if (peakPosition > inputPeakPosition)
		drawMeter(inputPeakPosition, inputPeakHoldPosition, diffNominalColor, diffWarningColor, diffErrorColor);

	if (magnitudePosition - 3 >= minimumPosition)
		painter.fillRect(x, magnitudePosition - 3, width, 3,
			magnitudeColor);
}

void QGainWidget::paintEvent(QPaintEvent *event)
{
	uint64_t ts = os_gettime_ns();
	qreal timeSinceLastRedraw = (ts - lastRedrawTime) * 0.000000001;
	calculateBallistics(ts, timeSinceLastRedraw);

	QPainter painter(this);
	/*Draw cached background*/
	int width = this->width();
	int height = this->height();

	int maxChannelNr = _channels;
	for (int channelNr = 0; channelNr < maxChannelNr; channelNr++) {
		if (vertical)
			paintVMeter(painter, channelNr * 4, 8, 3, height - 10,
				displayMagnitude[channelNr],
				displayPeak[channelNr],
				displayInputPeak[channelNr],
				displayPeakHold[channelNr],
				displayInputPeakHold[channelNr]);
		else
			paintHMeter(painter, 5, channelNr * 4, width - 5, 3,
				displayMagnitude[channelNr],
				displayPeak[channelNr],
				displayInputPeak[channelNr],
				displayPeakHold[channelNr],
				displayInputPeakHold[channelNr]);

		// -60.0, -50.0, -9.0);
		// By not drawing the input meter boxes the user can
		// see that the audio stream has been stopped, without
		// having too much visual impact.
		if (vertical)
			paintInputMeter(painter, channelNr * 4, 3, 3, 3,
				displayInputPeakHold[channelNr]);
		else
			paintInputMeter(painter, 0, channelNr * 4, 3, 3,
				displayInputPeakHold[channelNr]);
	}

	/*Draw Handle*/
	int w = vertical ? maxChannelNr * 4 : _handleWidth;
	int h = vertical ? _handleWidth : maxChannelNr * 4;
	int x = this->rect().topLeft().x() +
		vertical ? 0 : (_scale * (double)(width - _handleWidth));
	int y = this->rect().topLeft().y() +
		vertical ? (_scale * (double)(height - _handleWidth)): 0;
	painter.fillRect(x, y, w, h, Qt::blue);

	lastRedrawTime = ts;
}

void QGainWidget::mousePressEvent(QMouseEvent *event)
{
	QPoint point = event->pos();
	QPointF pointf = event->localPos();
	switch (event->button()) {
	case Qt::RightButton:
		_scale = 0.5;
		_db = 0.0;
		_mul = 1.0;
		blog(LOG_INFO, "[%f db, %f]", _db, _mul);
		break;
	case Qt::LeftButton:
		if (vertical)
			_scale = clamp((pointf.y() / (double)(this->height() - _handleWidth)), 0, 1.0);
		else
			_scale = clamp((pointf.x() / (double)(this->width() - _handleWidth)), 0, 1.0);
		_db = lerp(_scale, _minDb, _maxDb);
		_mul = db_to_mul((float)_db);
		blog(LOG_INFO, "[%f db, %f]", _db, _mul);
		_mousePressed = true;
		break;
	default:
		return;
	}

	obs_data_t *settings = obs_source_get_settings(_source);
	obs_data_set_double(settings, S_GAIN_DB, (double)_db);
	obs_source_update(_source, settings);
	obs_data_release(settings);
	obs_source_update_properties(_source);

	
//	blog(LOG_INFO, "[%f / %f] (%f)", pointf.x(), this->width(), (pointf.x() / (double)this->width()));
//	repaint();
}

void QGainWidget::mouseReleaseEvent(QMouseEvent *event)
{
	_mousePressed = false;
}

void QGainWidget::mouseMoveEvent(QMouseEvent *event)
{
	if (_mousePressed) {
		QPoint point = event->pos();
		QPointF pointf = event->localPos();
		//	blog(LOG_INFO, "[%f / %f] (%f)", pointf.x(), this->width(), (pointf.x() / (double)this->width()));

		if (vertical)
			_scale = clamp((pointf.y() / (double)(this->height() - _handleWidth)), 0, 1.0);
		else
			_scale = clamp((pointf.x() / (double)(this->width() - _handleWidth)), 0, 1.0);
		_db = lerp(_scale, _minDb, _maxDb);
		_mul = db_to_mul((float)_db);
		blog(LOG_INFO, "[%f db, %f]", _db, _mul);

		obs_data_t *settings = obs_source_get_settings(_source);
		obs_data_set_double(settings, S_GAIN_DB, (double)_db);
		obs_source_update(_source, settings);
		obs_data_release(settings);
		obs_source_update_properties(_source);
	}
}

void QGainWidget::mouseDoubleClickEvent(QMouseEvent *event)
{

}

void QGainWidget::dragMoveEvent(QDragMoveEvent *event)
{

}

void QGainWidget::dragLeaveEvent(QDragLeaveEvent *event)
{

}

void QGainWidget::dragEnterEvent(QDragEnterEvent *event)
{

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
		struct gain_data *gf = static_cast<struct gain_data*>(bzalloc(sizeof(*gf)));
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
				/* apply gain*/
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
					gf->input_mag[c] = sqrtf(input_sum / audio->frames);
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

		obs_properties_set_param(ppts, gf, nullptr);
		return ppts;
	}

	void *gain_ui(void *source, void *parent)
	{
		QWidget *parentWidget = static_cast<QWidget*>(parent);
		return new QGainWidget(parentWidget, Qt::WindowFlags(),
			static_cast<obs_source_t*>(source));
	}

}
