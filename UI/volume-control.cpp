#include "volume-control.hpp"
#include "qt-wrappers.hpp"
#include "obs-app.hpp"
#include "mute-checkbox.hpp"
#include "slider-absoluteset-style.hpp"
#include <obs-audio-controls.h>
#include <util/platform.h>
#include <util/threading.h>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QVariant>
#include <QSlider>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <string>
#include <math.h>

using namespace std;

#define CLAMP(x, min, max) ((x) < min ? min : ((x) > max ? max : (x)))

QWeakPointer<VolumeMeterTimer> VolumeMeter::updateTimer;

void VolControl::OBSVolumeChanged(void *data, float db)
{
	Q_UNUSED(db);
	VolControl *volControl = static_cast<VolControl*>(data);

	QMetaObject::invokeMethod(volControl, "VolumeChanged");
}

void VolControl::OBSVolumeLevel(void *data,
	const float magnitude[MAX_AUDIO_CHANNELS],
	const float peak[MAX_AUDIO_CHANNELS],
	const float inputPeak[MAX_AUDIO_CHANNELS],
	const struct audio_data audio_buffer,
	const struct audio_data fft_buffer)
{
	VolControl *volControl = static_cast<VolControl*>(data);

	volControl->volMeter->setLevels(magnitude, peak, inputPeak, audio_buffer, fft_buffer);
}

void VolControl::OBSVolumeMuted(void *data, calldata_t *calldata)
{
	VolControl *volControl = static_cast<VolControl*>(data);
	bool muted = calldata_bool(calldata, "muted");

	QMetaObject::invokeMethod(volControl, "VolumeMuted",
			Q_ARG(bool, muted));
}

void VolControl::VolumeChanged()
{
	slider->blockSignals(true);
	slider->setValue((int) (obs_fader_get_deflection(obs_fader) * 100.0f));
	slider->blockSignals(false);

	updateText();
}

void VolControl::VolumeMuted(bool muted)
{
	if (mute->isChecked() != muted)
		mute->setChecked(muted);
}

void VolControl::SetMuted(bool checked)
{
	obs_source_set_muted(source, checked);
}

void VolControl::SliderChanged(int vol)
{
	obs_fader_set_deflection(obs_fader, float(vol) * 0.01f);
	updateText();
}

void VolControl::updateText()
{
	QString db = QString::number(obs_fader_get_db(obs_fader), 'f', 1)
			.append(" dB");
	volLabel->setText(db);

	bool muted = obs_source_muted(source);
	const char *accTextLookup = muted
		? "VolControl.SliderMuted"
		: "VolControl.SliderUnmuted";

	QString sourceName = obs_source_get_name(source);
	QString accText = QTStr(accTextLookup).arg(sourceName, db);

	slider->setAccessibleName(accText);
}

QString VolControl::GetName() const
{
	return nameLabel->text();
}

void VolControl::SetName(const QString &newName)
{
	nameLabel->setText(newName);
}

void VolControl::EmitConfigClicked()
{
	emit ConfigClicked();
}

void VolControl::SetMeterDecayRate(qreal q)
{
	volMeter->setPeakDecayRate(q);
}

VolControl::VolControl(OBSSource source_, bool showConfig)
	: source        (source_),
	  levelTotal    (0.0f),
	  levelCount    (0.0f),
	  obs_fader     (obs_fader_create(OBS_FADER_CUBIC)),
	  obs_volmeter  (obs_volmeter_create(OBS_FADER_LOG))
{
	QHBoxLayout *volLayout  = new QHBoxLayout();
	QVBoxLayout *mainLayout = new QVBoxLayout();
	QHBoxLayout *textLayout = new QHBoxLayout();
	QHBoxLayout *botLayout  = new QHBoxLayout();

	nameLabel = new QLabel();
	volLabel  = new QLabel();
	volMeter  = new VolumeMeter(0, obs_volmeter);
	mute      = new MuteCheckBox();
	slider    = new QSlider(Qt::Horizontal);

	QFont font = nameLabel->font();
	font.setPointSize(font.pointSize()-1);

	QString sourceName = obs_source_get_name(source);

	nameLabel->setText(sourceName);
	nameLabel->setFont(font);
	volLabel->setFont(font);
	slider->setMinimum(0);
	slider->setMaximum(100);

//	slider->setMaximumHeight(13);

	textLayout->setContentsMargins(0, 0, 0, 0);
	textLayout->addWidget(nameLabel);
	textLayout->addWidget(volLabel);
	textLayout->setAlignment(nameLabel, Qt::AlignLeft);
	textLayout->setAlignment(volLabel,  Qt::AlignRight);

	bool muted = obs_source_muted(source);
	mute->setChecked(muted);
	mute->setAccessibleName(
			QTStr("VolControl.Mute").arg(sourceName));

	volLayout->addWidget(slider);
	volLayout->addWidget(mute);
	volLayout->setSpacing(5);

	botLayout->setContentsMargins(0, 0, 0, 0);
	botLayout->setSpacing(0);
	botLayout->addLayout(volLayout);

	if (showConfig) {
		config = new QPushButton(this);
		config->setProperty("themeID", "configIconSmall");
		config->setFlat(true);
		config->setSizePolicy(QSizePolicy::Maximum,
				QSizePolicy::Maximum);
		config->setMaximumSize(22, 22);
		config->setAutoDefault(false);

		config->setAccessibleName(QTStr("VolControl.Properties")
				.arg(sourceName));

		connect(config, &QAbstractButton::clicked,
				this, &VolControl::EmitConfigClicked);

		botLayout->addWidget(config);
	}

	mainLayout->setContentsMargins(4, 4, 4, 4);
	mainLayout->setSpacing(2);
	mainLayout->addItem(textLayout);
	mainLayout->addWidget(volMeter);
	mainLayout->addItem(botLayout);

	setLayout(mainLayout);

	obs_fader_add_callback(obs_fader, OBSVolumeChanged, this);
	obs_volmeter_add_callback(obs_volmeter, OBSVolumeLevel, this);

	signal_handler_connect(obs_source_get_signal_handler(source),
			"mute", OBSVolumeMuted, this);

	QWidget::connect(slider, SIGNAL(valueChanged(int)),
			this, SLOT(SliderChanged(int)));
	QWidget::connect(mute, SIGNAL(clicked(bool)),
			this, SLOT(SetMuted(bool)));

	obs_fader_attach_source(obs_fader, source);
	obs_volmeter_attach_source(obs_volmeter, source);

	slider->setStyle(new SliderAbsoluteSetStyle(slider->style()));

	/* Call volume changed once to init the slider position and label */
	VolumeChanged();
}

VolControl::~VolControl()
{
	obs_fader_remove_callback(obs_fader, OBSVolumeChanged, this);
	obs_volmeter_remove_callback(obs_volmeter, OBSVolumeLevel, this);

	signal_handler_disconnect(obs_source_get_signal_handler(source),
			"mute", OBSVolumeMuted, this);

	obs_fader_destroy(obs_fader);
	obs_volmeter_destroy(obs_volmeter);
}

QColor VolumeMeter::getBackgroundNominalColor() const
{
	return backgroundNominalColor;
}

void VolumeMeter::setBackgroundNominalColor(QColor c)
{
	backgroundNominalColor = c;
}

QColor VolumeMeter::getBackgroundWarningColor() const
{
	return backgroundWarningColor;
}

void VolumeMeter::setBackgroundWarningColor(QColor c)
{
	backgroundWarningColor = c;
}

QColor VolumeMeter::getBackgroundErrorColor() const
{
	return backgroundErrorColor;
}

void VolumeMeter::setBackgroundErrorColor(QColor c)
{
	backgroundErrorColor = c;
}

QColor VolumeMeter::getForegroundNominalColor() const
{
	return foregroundNominalColor;
}

void VolumeMeter::setForegroundNominalColor(QColor c)
{
	foregroundNominalColor = c;
}

QColor VolumeMeter::getForegroundWarningColor() const
{
	return foregroundWarningColor;
}

void VolumeMeter::setForegroundWarningColor(QColor c)
{
	foregroundWarningColor = c;
}

QColor VolumeMeter::getForegroundErrorColor() const
{
	return foregroundErrorColor;
}

void VolumeMeter::setForegroundErrorColor(QColor c)
{
	foregroundErrorColor = c;
}

QColor VolumeMeter::getClipColor() const
{
	return clipColor;
}

void VolumeMeter::setClipColor(QColor c)
{
	clipColor = c;
}

QColor VolumeMeter::getMagnitudeColor() const
{
	return magnitudeColor;
}

void VolumeMeter::setMagnitudeColor(QColor c)
{
	magnitudeColor = c;
}

QColor VolumeMeter::getMajorTickColor() const
{
	return majorTickColor;
}

void VolumeMeter::setMajorTickColor(QColor c)
{
	majorTickColor = c;
}

QColor VolumeMeter::getMinorTickColor() const
{
	return minorTickColor;
}

void VolumeMeter::setMinorTickColor(QColor c)
{
	minorTickColor = c;
}

qreal VolumeMeter::getMinimumLevel() const
{
	return minimumLevel;
}

void VolumeMeter::setMinimumLevel(qreal v)
{
	minimumLevel = v;
}

qreal VolumeMeter::getWarningLevel() const
{
	return warningLevel;
}

void VolumeMeter::setWarningLevel(qreal v)
{
	warningLevel = v;
}

qreal VolumeMeter::getErrorLevel() const
{
	return errorLevel;
}

void VolumeMeter::setErrorLevel(qreal v)
{
	errorLevel = v;
}

qreal VolumeMeter::getClipLevel() const
{
	return clipLevel;
}

void VolumeMeter::setClipLevel(qreal v)
{
	clipLevel = v;
}

qreal VolumeMeter::getMinimumInputLevel() const
{
	return minimumInputLevel;
}

void VolumeMeter::setMinimumInputLevel(qreal v)
{
	minimumInputLevel = v;
}

qreal VolumeMeter::getPeakDecayRate() const
{
	return peakDecayRate;
}

void VolumeMeter::setPeakDecayRate(qreal v)
{
	peakDecayRate = v;
}

qreal VolumeMeter::getMagnitudeIntegrationTime() const
{
	return magnitudeIntegrationTime;
}

void VolumeMeter::setMagnitudeIntegrationTime(qreal v)
{
	magnitudeIntegrationTime = v;
}

qreal VolumeMeter::getPeakHoldDuration() const
{
	return peakHoldDuration;
}

void VolumeMeter::setPeakHoldDuration(qreal v)
{
	peakHoldDuration = v;
}

qreal VolumeMeter::getInputPeakHoldDuration() const
{
	return inputPeakHoldDuration;
}

void VolumeMeter::setInputPeakHoldDuration(qreal v)
{
	inputPeakHoldDuration = v;
}

void VolumeMeter::setMinimumClipHoldDuration(qreal v)
{
	clipHoldTime = v;
	_clipHoldTime = v * 0.000000001;
}

qreal VolumeMeter::getMinimumClipHoldDuration() const
{
	return clipHoldTime;
}

void VolumeMeter::setClipAnimationDuration(qreal v) {
	clipAnimationLength = v;
	_clipAnimationLength = v * 0.000000001;
}

qreal VolumeMeter::getClipAnimationDuration() const
{
	return clipAnimationLength;
}

VolumeMeter::VolumeMeter(QWidget *parent, obs_volmeter_t *obs_volmeter)
			: QWidget(parent), obs_volmeter(obs_volmeter)
{
	// Use a font that can be rendered small.
	tickFont = QFont("Arial");
	tickFont.setPixelSize(7);
	// Default meter color settings, they only show if
	// there is no stylesheet, do not remove.
	backgroundNominalColor.setRgb(0x26, 0x7f, 0x26);    // Dark green
	backgroundWarningColor.setRgb(0x7f, 0x7f, 0x26);    // Dark yellow
	backgroundErrorColor.setRgb(0x7f, 0x26, 0x26);      // Dark red
	foregroundNominalColor.setRgb(0x4c, 0xff, 0x4c);    // Bright green
	foregroundWarningColor.setRgb(0xff, 0xff, 0x4c);    // Bright yellow
	foregroundErrorColor.setRgb(0xff, 0x4c, 0x4c);      // Bright red
	clipColor.setRgb(0xff, 0xff, 0xff);                 // Bright white
	magnitudeColor.setRgb(0x00, 0x00, 0x00);            // Black
	majorTickColor.setRgb(0xff, 0xff, 0xff);            // Black
	minorTickColor.setRgb(0xcc, 0xcc, 0xcc);            // Black
	minimumLevel = -60.0;                               // -60 dB
	warningLevel = -20.0;                               // -20 dB
	errorLevel = -9.0;                                  //  -9 dB
	clipLevel = -0.5;                                   //  -0.5 dB
	minimumInputLevel = -50.0;                          // -50 dB
	peakDecayRate = 11.76;                              //  20 dB / 1.7 sec
	magnitudeIntegrationTime = 0.3;                     //  99% in 300 ms
	peakHoldDuration = 20.0;                            //  20 seconds
	inputPeakHoldDuration = 1.0;                        //  1 second

	//gets sample rate and speaker config
	struct obs_audio_info aoi;
	obs_get_audio_info(&aoi);
	obs_sample_rate = aoi.samples_per_sec;
	obs_speakers = aoi.speakers;
	
	handleChannelCofigurationChange();
	updateTimerRef = updateTimer.toStrongRef();
	if (!updateTimerRef) {
		updateTimerRef = QSharedPointer<VolumeMeterTimer>::create();
		updateTimerRef->start(34);
		updateTimer = updateTimerRef;
	}

	updateTimerRef->AddVolControl(this);
}

VolumeMeter::~VolumeMeter()
{
	updateTimerRef->RemoveVolControl(this);
}

void VolumeMeter::setLevels(
	const float magnitude[MAX_AUDIO_CHANNELS],
	const float peak[MAX_AUDIO_CHANNELS],
	const float inputPeak[MAX_AUDIO_CHANNELS])
{
	uint64_t ts = os_gettime_ns();
	QMutexLocker locker(&dataMutex);

	currentLastUpdateTime = ts;
	for (int channelNr = 0; channelNr < MAX_AUDIO_CHANNELS; channelNr++) {
		currentMagnitude[channelNr] = magnitude[channelNr];
		currentPeak[channelNr] = peak[channelNr];
		currentInputPeak[channelNr] = inputPeak[channelNr];
		//copy audio samples / fft data in
		memcpy(currentAudioData[channelNr], audio_buffer.data[channelNr], currentAudioDataSamples * sizeof(float));
		memcpy(currentFFTData[channelNr], fft_buffer.data[channelNr], currentFFTDataSamples * sizeof(float));
	}

	// In case there are more updates then redraws we must make sure
	// that the ballistics of peak and hold are recalculated.
	locker.unlock();
	calculateBallistics(ts);
}

inline void VolumeMeter::resetLevels()
{
	currentLastUpdateTime = 0;
	for (int channelNr = 0; channelNr < MAX_AUDIO_CHANNELS; channelNr++) {
		currentMagnitude[channelNr] = -M_INFINITE;
		currentPeak[channelNr] = -M_INFINITE;
		currentInputPeak[channelNr] = -M_INFINITE;

		displayMagnitude[channelNr] = -M_INFINITE;
		displayPeak[channelNr] = -M_INFINITE;
		displayPeakHold[channelNr] = -M_INFINITE;
		displayPeakHoldLastUpdateTime[channelNr] = 0;
		displayInputPeakHold[channelNr] = -M_INFINITE;
		displayInputPeakHoldLastUpdateTime[channelNr] = 0;
		
		//0 out the fft / waveform data
		memset(currentAudioData[channelNr], 0.0, AUDIO_OUTPUT_FRAMES * sizeof(float));
		memset(currentFFTData[channelNr], 0.0, AUDIO_OUTPUT_FRAMES * sizeof(float));
		memset(displayFFTData[channelNr], 0.0, AUDIO_OUTPUT_FRAMES * sizeof(float));
		memset(displayAudioData[channelNr], 0.0, AUDIO_OUTPUT_FRAMES * sizeof(float));
	}
}

inline void VolumeMeter::handleChannelCofigurationChange()
{
	QMutexLocker locker(&dataMutex);

	int currentNrAudioChannels = obs_volmeter_get_nr_channels(obs_volmeter_ptr);
	if (displayNrAudioChannels != currentNrAudioChannels || display_volume_meter_type != current_volume_meter_type || display_volume_options != current_volume_options) {
		displayNrAudioChannels = currentNrAudioChannels;
		display_volume_meter_type = current_volume_meter_type;
		display_volume_options = current_volume_options;
		// Make room for 3 pixels high meter, with one pixel between
		// each. Then 9 pixels below it for ticks and numbers.
		qreal tickWidth;
		if (display_volume_options & OBS_VOLUME_VERTICAL) {
			switch (display_volume_meter_type) {
			case OBS_VOLUME_METER_VIEW:
				tickWidth = (drawTickMarksVolume ? meterTickWidthVolume : 0);
				setMinimumSize(displayNrAudioChannels * meterBarWidthVolume + (drawTickMarksVolume ? meterTickWidthVolume : 0), 130);
				break;
			case OBS_WAVEFORM_VIEW:
				tickWidth = (drawTickMarksWave ? meterTickWidthWave : 0);
				setMinimumSize(displayNrAudioChannels * meterBarWidthWave + (drawTickMarksWave ? meterTickWidthWave : 0), 130);
				break;
			case OBS_FFT_VIEW:
				tickWidth = (drawTickMarksFFT ? meterTickWidthFFT : 0);
				setMinimumSize(displayNrAudioChannels * meterBarWidthFFT + (drawTickMarksFFT ? meterTickWidthFFT : 0), 130);
				break;
			default:
				tickWidth = (drawTickMarksVolume ? meterTickWidthVolume : 0);
				setMinimumSize(displayNrAudioChannels * meterBarWidthVolume + (drawTickMarksVolume ? meterTickWidthVolume : 0), 130);
			}
		}
		else {
			switch (display_volume_meter_type) {
			case OBS_VOLUME_METER_VIEW:
				tickWidth = (drawTickMarksVolume ? meterTickWidthVolume : 0);
				setMinimumSize(130, displayNrAudioChannels * meterBarWidthVolume + (drawTickMarksVolume ? meterTickWidthVolume : 0));
				break;
			case OBS_WAVEFORM_VIEW:
				tickWidth = (drawTickMarksWave ? meterTickWidthWave : 0);
				setMinimumSize(130, displayNrAudioChannels * meterBarWidthWave + (drawTickMarksWave ? meterTickWidthWave : 0));
				break;
			case OBS_FFT_VIEW:
				tickWidth = (drawTickMarksFFT ? meterTickWidthFFT : 0);
				setMinimumSize(130, displayNrAudioChannels * meterBarWidthFFT + (drawTickMarksFFT ? meterTickWidthFFT : 0));
				break;
			default:
				tickWidth = (drawTickMarksVolume ? meterTickWidthVolume : 0);
				setMinimumSize(130, displayNrAudioChannels * meterBarWidthVolume + (drawTickMarksVolume ? meterTickWidthVolume : 0));
			}
		}
		adjustSize();

		QSize tickPaintCacheSize;

		switch (display_volume_options)
		{
		case OBS_VOLUME_VERTICAL:
			tickPaintCacheSize = QSize(tickWidth + 1.0, size().height());
			break;
		default:
			tickPaintCacheSize = QSize(size().width(), tickWidth + 1.0);
		}

		delete tickPaintCache;
		tickPaintCache = new QPixmap(tickPaintCacheSize);

		QColor clearColor(0, 0, 0, 0);
		tickPaintCache->fill(clearColor);

		QPainter tickPainter(tickPaintCache);
		//horizontal only
		switch (display_volume_meter_type) {
			case OBS_VOLUME_METER_VIEW:
				if (drawTickMarksVolume) {
					paintTicks(tickPainter, 6, 0, tickPaintCacheSize.width() - 6,
						tickPaintCacheSize.height());
				}
				break;
			case OBS_WAVEFORM_VIEW:
				if (drawTickMarksWave) {

				}
				break;
			case OBS_FFT_VIEW:
				if (drawTickMarksFFT) {
					paintTicksFFT(tickPainter, 6, 0, tickPaintCacheSize.width() - 6,
						tickPaintCacheSize.height());
				}
				break;
			default:
				if (drawTickMarksVolume) {
					paintTicks(tickPainter, 6, 0, tickPaintCacheSize.width() - 6,
						tickPaintCacheSize.height());
				}
		}

		tickPainter.end();

		resetLevels();
	}
}

inline bool VolumeMeter::detectIdle(uint64_t ts)
{
	float timeSinceLastUpdate = (ts - currentLastUpdateTime) * 0.000000001;
	if (timeSinceLastUpdate > 0.5) {
		resetLevels();
		return true;
	} else {
		return false;
	}
}

inline void VolumeMeter::calculateBallisticsForChannel(int channelNr,
	uint64_t ts, qreal timeSinceLastRedraw)
{
	for (int i = 0; i < currentFFTDataSamples; i++) {
		if (currentFFTData[channelNr][i] >= displayFFTData[channelNr][i] || displayFFTData[channelNr][i]) {
			displayFFTData[channelNr][i] = currentFFTData[channelNr][i];
		} else {
			// Decay of peak is 20 dB / 1.7 seconds.
			qreal decay = peakDecayRate * timeSinceLastRedraw;
			displayFFTData[channelNr][i] = CLAMP(displayFFTData[channelNr][i] - decay, currentFFTData[channelNr][i], 0);
		}
	}
	
	if (currentPeak[channelNr] >= displayPeak[channelNr] ||
		isnan(displayPeak[channelNr])) {
		// Attack of peak is immediate.
		displayPeak[channelNr] = currentPeak[channelNr];
	} else {
		// Decay of peak is 40 dB / 1.7 seconds for Fast Profile
		// 20 dB / 1.7 seconds for Medium Profile (Type I PPM)
		// 24 dB / 2.8 seconds for Slow Profile (Type II PPM)
		qreal decay = peakDecayRate * timeSinceLastRedraw;
		displayPeak[channelNr] = CLAMP(displayPeak[channelNr] - decay,
			currentPeak[channelNr], 0);
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
		displayMagnitude[channelNr] =
			currentMagnitude[channelNr];
	} else {
		// A VU meter will integrate to the new value to 99% in 300 ms.
		// The calculation here is very simplified and is more accurate
		// with higher frame-rate.
		qreal attack = (currentMagnitude[channelNr] -
			displayMagnitude[channelNr]) *
				(timeSinceLastRedraw /
			magnitudeIntegrationTime) * 0.99;
		displayMagnitude[channelNr] = CLAMP(
			displayMagnitude[channelNr] + attack,
			minimumLevel, 0);
	}
}

inline void VolumeMeter::calculateBallistics(uint64_t ts,
	qreal timeSinceLastRedraw)
{
	QMutexLocker locker(&dataMutex);

	for (int channelNr = 0; channelNr < MAX_AUDIO_CHANNELS; channelNr++) {
		calculateBallisticsForChannel(channelNr, ts,
			timeSinceLastRedraw);
	}
}

void VolumeMeter::paintInputMeter(QPainter &painter, int x, int y,
	int width, int height, float peakHold)
{
	QMutexLocker locker(&dataMutex);

	if (peakHold < minimumInputLevel) {
		painter.fillRect(x, y, width, height, backgroundNominalColor);
	} else if (peakHold < warningLevel) {
		painter.fillRect(x, y, width, height, foregroundNominalColor);
	} else if (peakHold < errorLevel) {
		painter.fillRect(x, y, width, height, foregroundWarningColor);
	} else if (peakHold <= clipLevel) {
		painter.fillRect(x, y, width, height, foregroundErrorColor);
	} else {
		painter.fillRect(x, y, width, height, clipColor);
	}
}

void VolumeMeter::paintTicks(QPainter &painter, int x, int y,
	int width, int height)
{
	if (display_volume_options & OBS_VOLUME_VERTICAL) {
		qreal scale = height / minimumLevel;

		painter.setFont(tickFont);
		painter.setPen(majorTickColor);

		// Draw major tick lines and numeric indicators.
		for (int i = 0; i >= minimumLevel; i -= 5) {
			int position = y + (i * scale) - 1;
			QString str = QString::number(i);

			if (i == 0 || i == -5) {
				painter.drawText(width, position - 3, str);
			}
			else {
				painter.drawText(width, position - 5, str);
			}
			//painter.drawLine(position, y, position, y + 2);
			painter.drawLine(x+width, position, x+width-2,position);
		}

		// Draw minor tick lines.
		painter.setPen(minorTickColor);
		for (int i = 0; i >= minimumLevel; i--) {
			//int position = x + width - (i * scale) - 1;
			int position = y + (i * scale) - 1;
			if (i % 5 != 0) {
				painter.drawLine(x + width, position, x + width - 1, position);
				//painter.drawLine(position, y, position, y + 1);
			}
		}
	} else {
		qreal scale = width / minimumLevel;

		painter.setFont(tickFont);
		painter.setPen(majorTickColor);

		// Draw major tick lines and numeric indicators.
		for (int i = 0; i >= minimumLevel; i -= 5) {
			int position = x + width - (i * scale) - 1;
			QString str = QString::number(i);

			if (i == 0 || i == -5) {
				painter.drawText(position - 3, height, str);
			}
			else {
				painter.drawText(position - 5, height, str);
			}
			painter.drawLine(position, y, position, y + 2);
		}

		// Draw minor tick lines.
		painter.setPen(minorTickColor);
		for (int i = 0; i >= minimumLevel; i--) {
			int position = x + width - (i * scale) - 1;

			if (i % 5 != 0) {
				painter.drawLine(position, y, position, y + 1);
			}
		}
	}
}

void VolumeMeter::paintFFT(QPainter &painter, int x, int y,
	int width, int height, int channel)
{
	//qreal y_scale = height / minimumLevel;
	//qreal x_scale = width / minimumLevel;
	qreal m_level = -120;//minimumLevel;
	qreal y_scale = (double)height / m_level;

	QMutexLocker locker(&dataMutex);

	uint64_t ts = os_gettime_ns();

	int minimumPositionX = x + 0;
	int maximumPositionX = x + width;

	int minimumPositionY = y + height;
	int maximumPositionY = y;

	double warningPosition = maximumPositionY + (warningLevel * y_scale);
	double errorPosition = maximumPositionY + (errorLevel * y_scale);

	double nominalLength = minimumPositionY - warningPosition;
	double warningLength = warningPosition - errorPosition;
	double errorLength = errorPosition - maximumPositionY;

	double clipPosition = maximumPositionY + (errorLevel / 4.0f * y_scale);
	double clipLength = errorLength / 4.0f;

	double norminal = m_level;
	double warn = warningLevel;
	double error = errorLevel;
	//audio_data* pcm_data = obs_volmeter_ptr->circle_buffer;
	size_t samples = currentFFTDataSamples;
	qreal spacerWidth = (double)width / (double)samples;
	qreal dataWidth = spacerWidth;
	if (dataWidth < 1.0) {
		dataWidth = 1.0;
	}

	locker.unlock();

	for (int i = 0; i < samples; i++) {
		float _sample = currentAudioData[channel][i];
		//stuffed hidden data
		float _sample_i = currentAudioData[channel][i + samples];
		currentPowerSpectra[channel][i] = (_sample*_sample) * (_sample_i*_sample_i);
	}

	//fill downwards
	//error
	painter.fillRect(
		x, maximumPositionY,
		width, errorLength,
		backgroundErrorColor);

	//warning
	painter.fillRect(
		x, errorPosition,
		width, warningLength,
		backgroundWarningColor);

	//nominal
	painter.fillRect(
		x, warningPosition,
		width, nominalLength,
		backgroundNominalColor);

	//ignore 0 (the DC Offset)
	for (int i = 1; i < samples; i++) {
		float mag = sqrtf(currentPowerSpectra[channel][i]) / samples;
		double ampl = 20.0f * log10(mag);
	
		double drawPosition = maximumPositionY + (ampl * y_scale);

		if (ampl < m_level) {
			//~nothing
		}
		else if (ampl < warningLevel) {
			//nominal
			painter.fillRect(
				x + i*spacerWidth, drawPosition,
				dataWidth, minimumPositionY - drawPosition,
				foregroundNominalColor);
		}
		else if (ampl < errorLevel) {
			//warning
			painter.fillRect(
				x + i*spacerWidth, drawPosition,
				dataWidth, warningPosition - drawPosition,
				foregroundWarningColor);

			painter.fillRect(
				x + i*spacerWidth, warningPosition,
				dataWidth, nominalLength,
				foregroundNominalColor);
		}
		else if (ampl <= 0.0){
			//error
			painter.fillRect(
				x + i*spacerWidth, drawPosition,
				dataWidth, errorPosition - drawPosition,
				foregroundErrorColor);

			painter.fillRect(
				x + i*spacerWidth, errorPosition,
				dataWidth, warningLength,
				foregroundWarningColor);

			painter.fillRect(
				x + i*spacerWidth, warningPosition,
				dataWidth, nominalLength,
				foregroundNominalColor);
		}
		else {
			//also do the cliping thing
			painter.fillRect(
				x + i*spacerWidth, maximumPositionY,
				dataWidth, errorLength,
				foregroundErrorColor);

			painter.fillRect(
				x + i*spacerWidth, errorPosition,
				dataWidth, warningLength,
				foregroundWarningColor);

			painter.fillRect(
				x + i*spacerWidth, warningPosition,
				dataWidth, nominalLength,
				foregroundNominalColor);
		}

	}
}

void VolumeMeter::paintWaveForm(QPainter &painter, int x, int y,
	int width, int height, int channel) {
	//qreal y_scale = height / minimumLevel;
	qreal scale = width / minimumLevel;
	qreal y_scale = height / minimumLevel;

	QMutexLocker locker(&dataMutex);

	uint64_t ts = os_gettime_ns();

	int minimumPosition = x + 0;
	int maximumPosition = x + width;

	//audio_data* pcm_data = obs_volmeter_ptr->circle_buffer;
	size_t samples = currentAudioDataSamples;
	qreal dataWidth = (double)width / (double)samples;
	if (dataWidth < 1.0) {
		dataWidth = 1.0;
	}

	locker.unlock();
	
	painter.fillRect(
		minimumPosition, y,
		width, height,
		backgroundNominalColor);
	
	//pcm data ranges -1 -> 1 ergo 0 should be centered at 1/2 height
	QPen wave_pen;
	QPainterPath wave_form;
	qreal h2 = height / 2.0f;
	qreal h4 = height / 4.0f;
	qreal mid = y + h2;
	qreal target_y = y;
	wave_form.moveTo(x, mid);
	QColor wave_color = foregroundErrorColor;
	wave_pen.setColor(wave_color);
	for (int i = 0; i < samples; i++) {
		float _sample = currentAudioData[channel][i];
		//double _height = h4 * _sample;
		double _amplitude = fabs(_sample);
		if (_amplitude < 0.0) {
			target_y = y + h2;
		}
		else {
			target_y = (y + h2) - _amplitude * h2;
		}
		//wave_form.cubicTo(x + (i + 0.5)*dataWidth, _height, x + (i + 0.5)*dataWidth, _height, x + (i + 1)*dataWidth, mid);
		double x_start = x + i*dataWidth;
		double x_mid = x_start + dataWidth / 2.0;
		double x_end = x_start + dataWidth;
		double x_end_mid = x_mid + dataWidth;
		double y_point = mid + _sample*h2;
		wave_form.cubicTo(x_start, y_point, x_end, y_point, x_end, mid);
		//wave_form.cubicTo(x_mid,)
	}
	painter.setPen(wave_pen);
	painter.drawPath(wave_form);
}

void VolumeMeter::paintTicksFFT(QPainter &painter, int x, int y, int width, int height) {
	if (display_volume_options & OBS_VOLUME_VERTICAL) {
		/* TODO: Draw vertical */

	}
	else {
		qreal nyquist_rate = ((double)obs_sample_rate / 2.0);
		qreal scale = width / nyquist_rate;
		qreal major_tick_spacing = nyquist_rate / 12.0;
		qreal minor_tick_spacing = nyquist_rate / 60.0;

		painter.setFont(tickFont);
		painter.setPen(majorTickColor);

		// Draw major tick lines and numeric indicators.
		for (int i = 0; i <= nyquist_rate; i += major_tick_spacing) {
			int position = x + (i * scale) - 1;//x + width - (i * scale) - 1;
			QString str = QString::number(i);

			if (i >= nyquist_rate - (major_tick_spacing/2)) {
				painter.drawText(position - 20, height, str);
			}
			else {
				painter.drawText(position - 5, height, str);
			}
			painter.drawLine(position, y, position, y + 2);
		}

		// Draw minor tick lines.
		painter.setPen(minorTickColor);
		for (int i = 0; i <= nyquist_rate; i += minor_tick_spacing) {
			int position = x + (i * scale) - 1;
			painter.drawLine(position, y, position, y + 1);
		}
	}
}

void VolumeMeter::paintMeter(QPainter &painter, int x, int y,
	int width, int height, float magnitude, float peak, float peakHold)
{
	qreal scale = width / minimumLevel;

	QMutexLocker locker(&dataMutex);
	int minimumPosition     = x + 0;
	int maximumPosition     = x + width;
	int magnitudePosition   = x + width - (magnitude * scale);
	int peakPosition        = x + width - (peak * scale);
	int peakHoldPosition    = x + width - (peakHold * scale);
	int warningPosition     = x + width - (warningLevel * scale);
	int errorPosition       = x + width - (errorLevel * scale);

	int nominalLength       = warningPosition - minimumPosition;
	int warningLength       = errorPosition - warningPosition;
	int errorLength         = maximumPosition - errorPosition;
	
	int clipPosition	= errorPosition + (errorLength * 3 / 4);
	int clipLength		= maximumPosition - clipPosition;
	locker.unlock();

	if (peakPosition < minimumPosition) {
		painter.fillRect(
			minimumPosition, y,
			nominalLength, height,
			backgroundNominalColor);
		painter.fillRect(
			warningPosition, y,
			warningLength, height,
			backgroundWarningColor);
		painter.fillRect(
			errorPosition, y,
			errorLength, height,
			backgroundErrorColor);

	} else if (peakPosition < warningPosition) {
		painter.fillRect(
			minimumPosition, y,
			peakPosition - minimumPosition, height,
			foregroundNominalColor);
		painter.fillRect(
			peakPosition, y,
			warningPosition - peakPosition, height,
			backgroundNominalColor);
		painter.fillRect(
			warningPosition, y,
			warningLength, height,
			backgroundWarningColor);
		painter.fillRect(errorPosition, y,
			errorLength, height,
			backgroundErrorColor);

	} else if (peakPosition < errorPosition) {
		painter.fillRect(
			minimumPosition, y,
			nominalLength, height,
			foregroundNominalColor);
		painter.fillRect(
			warningPosition, y,
			peakPosition - warningPosition, height,
			foregroundWarningColor);
		painter.fillRect(
			peakPosition, y,
			errorPosition - peakPosition, height,
			backgroundWarningColor);
		painter.fillRect(
			errorPosition, y,
			errorLength, height,
			backgroundErrorColor);

	} else if (peakPosition < maximumPosition) {
		painter.fillRect(
			minimumPosition, y,
			nominalLength, height,
			foregroundNominalColor);
		painter.fillRect(
			warningPosition, y,
			warningLength, height,
			foregroundWarningColor);
		painter.fillRect(
			errorPosition, y,
			peakPosition - errorPosition, height,
			foregroundErrorColor);
		painter.fillRect(
			peakPosition, y,
			maximumPosition - peakPosition, height,
			backgroundErrorColor);

	} else {
		painter.fillRect(
			minimumPosition, y,
			nominalLength, height,
			foregroundNominalColor);
		painter.fillRect(
			warningPosition, y,
			warningLength, height,
			foregroundWarningColor);
		painter.fillRect(
			errorPosition, y,
			errorLength, height,
			foregroundErrorColor);
		//we've clipped set the new timer
		clipTime = ts + _clipHoldTime;
		hasClipped = true;
	}
	
	//draw an indicator at the end for a minimum length of time
	if (hasClipped || ts < clipTime) {
		uint64_t timeSinceClip = ts - clipTime;
		//square tick / flash
		uint64_t remainder = timeSinceClip % _clipAnimationLength;
		if (squareTick) {
			if (remainder < (_clipAnimationLength / 2)) {
				painter.fillRect(
					clipPosition, y,
					clipLength, height,
					clipColor);
			}
		}
	}

	if (peakHoldPosition - 3 < minimumPosition) {
		// Peak-hold below minimum, no drawing.

	} else if (peakHoldPosition < warningPosition) {
		painter.fillRect(
			peakHoldPosition - 3,  y,
			3, height,
			foregroundNominalColor);

	} else if (peakHoldPosition < errorPosition) {
		painter.fillRect(
			peakHoldPosition - 3, y,
			3, height,
			foregroundWarningColor);

	} else if (peakHoldPosition < maximumPosition) {
		painter.fillRect(
			peakHoldPosition - 3, y,
			3, height,
			foregroundErrorColor);
	} else {
		painter.fillRect(
			maximumPosition - 3, y,
			3, height,
			clipColor);
	}

	if (magnitudePosition - 3 < minimumPosition) {
		// Magnitude below minimum, no drawing.

	} else if (magnitudePosition < warningPosition) {
		painter.fillRect(
			magnitudePosition - 3, y,
			3, height,
			magnitudeColor);

	} else if (magnitudePosition < errorPosition) {
		painter.fillRect(
			magnitudePosition - 3, y,
			3, height,
			magnitudeColor);

	} else if (magnitudePosition < maximumPosition) {
		painter.fillRect(
			magnitudePosition - 3, y,
			3, height,
			magnitudeColor);
	} else {
		painter.fillRect(
			maximumPosition - 3, y,
			3, height,
			clipColor
		);
	}
	
		//idle meter at far left end
	if (peakHold < minimumInputLevel) {
			
	}
	else if (peakHold < warningLevel) {
		//painter.fillRect(x, y, width, height, foregroundNominalColor);
		painter.fillRect(
			minimumPosition, y,
			3, height,
			foregroundNominalColor);
	}
	else if (peakHold < errorLevel) {
		//painter.fillRect(x, y, width, height, foregroundWarningColor);
		painter.fillRect(
			minimumPosition, y,
			3, height,
			foregroundWarningColor);
	}
	else if (peakHold <= clipLevel) {
		//painter.fillRect(x, y, width, height, foregroundErrorColor);
		painter.fillRect(
			minimumPosition, y,
			3, height,
			foregroundErrorColor);
	}
	else {
		//painter.fillRect(x, y, width, height, clipColor);
		painter.fillRect(
			minimumPosition, y,
			3, height,
			clipColor);
	}
}

void VolumeMeter::mousePressEvent(QMouseEvent *event) {
	//hasClipped = false;
}

void VolumeMeter::mouseDoubleClickEvent(QMouseEvent *event)
{	
	static uint64_t ts;
	static uint64_t last_ts = 0;//0xFFFFFFFFFFFFFFFF;
	hasClipped = false;
	ts = os_gettime_ns();
	if (ts < last_ts + 1000000000) {
		switch (current_volume_meter_type) {
		case OBS_FFT_VIEW:
			current_volume_meter_type = OBS_VOLUME_METER_VIEW;
			break;
		default:
			current_volume_meter_type = (obs_volume_meter_type)(current_volume_meter_type + 1);
		}
	}
	last_ts = ts;
}

void VolumeMeter::paintEvent(QPaintEvent *event)
{
	UNUSED_PARAMETER(event);

	uint64_t ts = os_gettime_ns();
	qreal timeSinceLastRedraw = (ts - lastRedrawTime) * 0.000000001;

	handleChannelCofigurationChange();
	calculateBallistics(ts, timeSinceLastRedraw);
	
	int width  = size().width();
	int height = size().height();
	
	bool idle = detectIdle(ts);

	// Actual painting of the widget starts here.
	QPainter painter(this);
	if (display_volume_options & OBS_VOLUME_VERTICAL) {
		painter.drawPixmap(0, 0, *tickPaintCache);
	} else {
		painter.drawPixmap(0, height - 9, *tickPaintCache);
	}

	for (int channelNr = 0; channelNr < displayNrAudioChannels;
		channelNr++) {
		switch (display_volume_meter_type) {
		case OBS_VOLUME_METER_VIEW:
			paintMeter(painter,
				5, channelNr * meterBarWidthVolume, width - 5, meterBarWidthVolume - 1,
				displayMagnitude[channelNr], displayPeak[channelNr],
				displayPeakHold[channelNr]);
			break;
		case OBS_WAVEFORM_VIEW:
			paintWaveForm(painter,
				5, channelNr * meterBarWidthWave, width - 5, meterBarWidthWave-1,
				channelNr);
			break;
		case OBS_FFT_VIEW:
			paintFFT(painter,
				5, channelNr * meterBarWidthFFT, width - 5, meterBarWidthFFT-1,
				channelNr);
			break;
		default:
			paintMeter(painter,
				5, channelNr * meterBarWidthVolume, width - 5, meterBarWidthVolume - 1,
				displayMagnitude[channelNr], displayPeak[channelNr],
				displayPeakHold[channelNr]);
		}
	}

	lastRedrawTime = ts;
}

void VolumeMeterTimer::AddVolControl(VolumeMeter *meter)
{
	volumeMeters.push_back(meter);
}

void VolumeMeterTimer::RemoveVolControl(VolumeMeter *meter)
{
	volumeMeters.removeOne(meter);
}

void VolumeMeterTimer::timerEvent(QTimerEvent*)
{
	for (VolumeMeter *meter : volumeMeters)
		meter->update();
}
