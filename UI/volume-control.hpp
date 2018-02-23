#pragma once

#include <obs.hpp>
#include <QWidget>
#include <QSharedPointer>
#include <QTimer>
#include <QMutex>
#include <QList>

class QPushButton;
class VolumeMeterTimer;

class VolumeMeter : public QWidget
{
	Q_OBJECT
	Q_PROPERTY(QColor backgroundNominalColor
		READ getBackgroundNominalColor
		WRITE setBackgroundNominalColor DESIGNABLE true)
	Q_PROPERTY(QColor backgroundWarningColor
		READ getBackgroundWarningColor
		WRITE setBackgroundWarningColor DESIGNABLE true)
	Q_PROPERTY(QColor backgroundErrorColor
		READ getBackgroundErrorColor
		WRITE setBackgroundErrorColor DESIGNABLE true)
	Q_PROPERTY(QColor foregroundNominalColor
		READ getForegroundNominalColor
		WRITE setForegroundNominalColor DESIGNABLE true)
	Q_PROPERTY(QColor foregroundWarningColor
		READ getForegroundWarningColor
		WRITE setForegroundWarningColor DESIGNABLE true)
	Q_PROPERTY(QColor foregroundErrorColor
		READ getForegroundErrorColor
		WRITE setForegroundErrorColor DESIGNABLE true)
	Q_PROPERTY(QColor clipColor
		READ getClipColor
		WRITE setClipColor DESIGNABLE true)
	Q_PROPERTY(QColor magnitudeColor
		READ getMagnitudeColor
		WRITE setMagnitudeColor DESIGNABLE true)
	Q_PROPERTY(QColor majorTickColor
		READ getMajorTickColor
		WRITE setMajorTickColor DESIGNABLE true)
	Q_PROPERTY(QColor minorTickColor
		READ getMinorTickColor
		WRITE setMinorTickColor DESIGNABLE true)

	// Levels are denoted in dBFS.
	Q_PROPERTY(qreal minimumLevel
		READ getMinimumLevel
		WRITE setMinimumLevel DESIGNABLE true)
	Q_PROPERTY(qreal warningLevel
		READ getWarningLevel
		WRITE setWarningLevel DESIGNABLE true)
	Q_PROPERTY(qreal errorLevel
		READ getErrorLevel
		WRITE setErrorLevel DESIGNABLE true)
	Q_PROPERTY(qreal clipLevel
		READ getClipLevel
		WRITE setClipLevel DESIGNABLE true)
	Q_PROPERTY(qreal minimumInputLevel
		READ getMinimumInputLevel
		WRITE setMinimumInputLevel DESIGNABLE true)

	// Rates are denoted in dB/second.
	Q_PROPERTY(qreal peakDecayRate
		READ getPeakDecayRate
		WRITE setPeakDecayRate DESIGNABLE true)

	// Time in seconds for the VU meter to integrate over.
	Q_PROPERTY(qreal magnitudeIntegrationTime
		READ getMagnitudeIntegrationTime
		WRITE setMagnitudeIntegrationTime DESIGNABLE true)

	// Duration is denoted in seconds.
	Q_PROPERTY(qreal peakHoldDuration
		READ getPeakHoldDuration
		WRITE setPeakHoldDuration DESIGNABLE true)
	Q_PROPERTY(qreal inputPeakHoldDuration
		READ getInputPeakHoldDuration
		WRITE setInputPeakHoldDuration DESIGNABLE true)
		
	// Duration denoted in seconds, floating point, but used as a 64bit integer
	Q_PROPERTY(qreal clipHoldTime
		READ getMinimumClipHoldDuration
		WRITE setMinimumClipHoldDuration DESIGNABLE true)

	Q_PROPERTY(qreal clipAnimationLength
		READ getClipAnimationDuration
		WRITE setClipAnimationDuration DESIGNABLE true)

	/*
	Q_PROPERTY(qint16 clipAnimationTimingType
		READ getAnimationTimingType
		WRITE setAnimationTimingType DESIGNABLE true)
	*/

	/*
	Q_PROPERTY(qreal clipAnimationTimingParameter_0
		READ getAnimationTimingParameter_0
		WRITE setAnimationTimingParameter_0 DESIGNABLE true)

	Q_PROPERTY(qreal clipAnimationTimingParameter_1
		READ getAnimationTimingParameter_1
		WRITE setAnimationTimingParameter_1 DESIGNABLE true)

	Q_PROPERTY(qreal clipAnimationTimingParameter_2
		READ getAnimationTimingParameter_2
		WRITE setAnimationTimingParameter_2 DESIGNABLE true)

	Q_PROPERTY(qreal clipAnimationTimingParameter_3
		READ getAnimationTimingParameter_3
		WRITE setAnimationTimingParameter_3 DESIGNABLE true)
	*/

private:
	obs_volmeter_t *obs_volmeter;
	static QWeakPointer<VolumeMeterTimer> updateTimer;
	QSharedPointer<VolumeMeterTimer> updateTimerRef;

	inline void resetLevels();
	inline void handleChannelCofigurationChange();
	inline bool detectIdle(uint64_t ts);
	inline void calculateBallistics(uint64_t ts,
		qreal timeSinceLastRedraw=0.0);
	inline void calculateBallisticsForChannel(int channelNr,
		uint64_t ts, qreal timeSinceLastRedraw);

	void paintInputMeter(QPainter &painter, int x, int y,
		int width, int height, float peakHold);

	void paintFFT(QPainter &painter, int x, int y,
		int width, int height, int channel);
	
	void paintWaveForm(QPainter &painter, int x, int y,
		int width, int height, int channel);

	void paintMeter(QPainter &painter, int x, int y,
		int width, int height,
		float magnitude, float peak, float peakHold);
		
	void paintTicks(QPainter &painter, int x, int y, int width, int height);
	void paintTicksFFT(QPainter &painter, int x, int y, int width, int height);
	
	QMutex dataMutex;

	uint64_t currentLastUpdateTime = 0;
	float currentMagnitude[MAX_AUDIO_CHANNELS];
	float currentPeak[MAX_AUDIO_CHANNELS];
	float currentInputPeak[MAX_AUDIO_CHANNELS];
	
	size_t currentAudioDataSamples;
	size_t currentFFTDataSamples;

	float currentPowerSpectra[MAX_AUDIO_CHANNELS][AUDIO_OUTPUT_FRAMES];

	float currentAudioData[MAX_AUDIO_CHANNELS][AUDIO_OUTPUT_FRAMES];
	float currentFFTData[MAX_AUDIO_CHANNELS][AUDIO_OUTPUT_FRAMES];

	float displayAudioData[MAX_AUDIO_CHANNELS][AUDIO_OUTPUT_FRAMES];
	float displayFFTData[MAX_AUDIO_CHANNELS][AUDIO_OUTPUT_FRAMES];
	
	QPixmap *tickPaintCache = NULL;
	int displayNrAudioChannels = 0;
	float displayMagnitude[MAX_AUDIO_CHANNELS];
	float displayPeak[MAX_AUDIO_CHANNELS];
	float displayPeakHold[MAX_AUDIO_CHANNELS];
	uint64_t displayPeakHoldLastUpdateTime[MAX_AUDIO_CHANNELS];
	float displayInputPeakHold[MAX_AUDIO_CHANNELS];
	uint64_t displayInputPeakHoldLastUpdateTime[MAX_AUDIO_CHANNELS];

	QFont tickFont;
	QColor backgroundNominalColor;
	QColor backgroundWarningColor;
	QColor backgroundErrorColor;
	QColor foregroundNominalColor;
	QColor foregroundWarningColor;
	QColor foregroundErrorColor;
	QColor clipNominalColor;
	QColor clipWarningColor;
	QColor clipErrorColor;
	QColor clipColor;
	QColor magnitudeColor;
	QColor majorTickColor;
	QColor minorTickColor;
	qreal minimumLevel;
	qreal warningLevel;
	qreal errorLevel;
	qreal clipLevel;
	qreal minimumInputLevel;
	qreal peakDecayRate;
	qreal magnitudeIntegrationTime;
	qreal peakHoldDuration;
	qreal inputPeakHoldDuration;

	uint64_t lastRedrawTime = 0;
	
	uint64_t clipTime = 0;

	qreal clipHoldTime = 3.0;
	uint64_t _clipHoldTime = 3000000000;

	qreal clipAnimationLength = 1.0;
	uint64_t _clipAnimationLength = 1000000000;

	uint16_t meterBarWidthVolume = 4;
	uint16_t meterBarWidthWave = 7;//6.0 + 1
	uint16_t meterBarWidthFFT = 19;//13;//6.0*2 + 1

	uint16_t meterTickWidthVolume = 8;
	uint16_t meterTickWidthWave = 8;
	uint16_t meterTickWidthFFT = 8;

	bool drawTickMarksVolume = true;
	bool drawTickMarksWave = false;
	bool drawTickMarksFFT = true;
	
	bool squareTick = true;
	bool pulseAnimation = false;

	bool hasClipped = false;

	enum obs_volume_meter_type    display_volume_meter_type = OBS_VOLUME_METER_VIEW;
	enum obs_volume_meter_options display_volume_options = OBS_VOLUME_HORIZONTAL;

	enum obs_volume_meter_type    current_volume_meter_type = OBS_VOLUME_METER_VIEW;
	enum obs_volume_meter_options current_volume_options = OBS_VOLUME_HORIZONTAL;

	uint32_t obs_sample_rate;
	enum speaker_layout obs_speakers;

public:
	explicit VolumeMeter(QWidget *parent = 0,
		obs_volmeter_t *obs_volmeter = 0);
	~VolumeMeter();

	void setLevels(
		const float magnitude[MAX_AUDIO_CHANNELS],
		const float peak[MAX_AUDIO_CHANNELS],
		const float inputPeak[MAX_AUDIO_CHANNELS],
		const struct audio_data audio_buffer,
		const struct audio_data fft_buffer);

	QColor getBackgroundNominalColor() const;
	void setBackgroundNominalColor(QColor c);
	QColor getBackgroundWarningColor() const;
	void setBackgroundWarningColor(QColor c);
	QColor getBackgroundErrorColor() const;
	void setBackgroundErrorColor(QColor c);
	QColor getForegroundNominalColor() const;
	void setForegroundNominalColor(QColor c);
	QColor getForegroundWarningColor() const;
	void setForegroundWarningColor(QColor c);
	QColor getForegroundErrorColor() const;
	void setForegroundErrorColor(QColor c);
	QColor getClipColor() const;
	void setClipColor(QColor c);
	QColor getMagnitudeColor() const;
	void setMagnitudeColor(QColor c);
	QColor getMajorTickColor() const;
	void setMajorTickColor(QColor c);
	QColor getMinorTickColor() const;
	void setMinorTickColor(QColor c);
	qreal getMinimumLevel() const;
	void setMinimumLevel(qreal v);
	qreal getWarningLevel() const;
	void setWarningLevel(qreal v);
	qreal getErrorLevel() const;
	void setErrorLevel(qreal v);
	qreal getClipLevel() const;
	void setClipLevel(qreal v);
	qreal getMinimumInputLevel() const;
	void setMinimumInputLevel(qreal v);
	qreal getPeakDecayRate() const;
	void setPeakDecayRate(qreal v);
	qreal getMagnitudeIntegrationTime() const;
	void setMagnitudeIntegrationTime(qreal v);
	qreal getPeakHoldDuration() const;
	void setPeakHoldDuration(qreal v);
	qreal getInputPeakHoldDuration() const;
	void setInputPeakHoldDuration(qreal v);
	
	qreal getMinimumClipHoldDuration() const;
	void setMinimumClipHoldDuration(qreal v);
	qreal getClipAnimationDuration() const;
	void setClipAnimationDuration(qreal v);

protected:
	void paintEvent(QPaintEvent *event);
	void mouseDoubleClickEvent(QMouseEvent *event);
	void mousePressEvent(QMouseEvent *event);
};

class VolumeMeterTimer : public QTimer {
	Q_OBJECT

public:
	inline VolumeMeterTimer() : QTimer() {}

	void AddVolControl(VolumeMeter *meter);
	void RemoveVolControl(VolumeMeter *meter);

protected:
	virtual void timerEvent(QTimerEvent *event) override;
	QList<VolumeMeter*> volumeMeters;
};

class QLabel;
class QSlider;
class MuteCheckBox;

class VolControl : public QWidget {
	Q_OBJECT

private:
	OBSSource source;
	QLabel          *nameLabel;
	QLabel          *volLabel;
	VolumeMeter     *volMeter;
	QSlider         *slider;
	MuteCheckBox    *mute;
	QPushButton     *config = nullptr;
	float           levelTotal;
	float           levelCount;
	obs_fader_t     *obs_fader;
	obs_volmeter_t  *obs_volmeter;

	static void OBSVolumeChanged(void *param, float db);
	static void OBSVolumeLevel(void *data,
		const float magnitude[MAX_AUDIO_CHANNELS],
		const float peak[MAX_AUDIO_CHANNELS],
		const float inputPeak[MAX_AUDIO_CHANNELS],
		const struct audio_data audio_buffer,
		const struct audio_data fft_buffer);
	static void OBSVolumeMuted(void *data, calldata_t *calldata);

	void EmitConfigClicked();

private slots:
	void VolumeChanged();
	void VolumeMuted(bool muted);

	void SetMuted(bool checked);
	void SliderChanged(int vol);
	void updateText();

signals:
	void ConfigClicked();

public:
	VolControl(OBSSource source, bool showConfig = false);
	~VolControl();

	inline obs_source_t *GetSource() const {return source;}

	QString GetName() const;
	void SetName(const QString &newName);

	void SetMeterDecayRate(qreal q);
};
