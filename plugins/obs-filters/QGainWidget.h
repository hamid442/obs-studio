#include <QWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QDragMoveEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QMetaObject>
#include <QLinearGradient>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>
#include <QBrush>
#include <cmath>
#include "util/threading.h"
#include "util/platform.h"

constexpr double lerp(double t, double a, double b)
{
	return (1 - t)*a + t * b;
}

constexpr double clamp(double t, double min, double max)
{
	if (t < min)
		return min;
	else if (t > max)
		return max;
	else
		return t;
}

#define CLAMP(x, min, max) (clamp(x, min, max))

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

class QGainWidget : public QWidget{
//	Q_OBJECT
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
	Q_PROPERTY(QColor diffNominalColor
		READ getDiffNominalColor
		WRITE setDiffNominalColor DESIGNABLE true)
	Q_PROPERTY(QColor diffWarningColor
		READ getDiffWarningColor
		WRITE setDiffWarningColor DESIGNABLE true)
	Q_PROPERTY(QColor diffErrorColor
		READ getDiffErrorColor
		WRITE setDiffErrorColor DESIGNABLE true)
	Q_PROPERTY(QColor magnitudeColor
		READ getMagnitudeColor
		WRITE setMagnitudeColor DESIGNABLE true)

	Q_PROPERTY(QColor clipColor
		READ getClipColor
		WRITE setClipColor DESIGNABLE true)

	Q_PROPERTY(qreal clipLevel
		READ getClipLevel
		WRITE setClipLevel DESIGNABLE true)

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
private slots:
	void redrawMeter()
	{
		repaint();
	}
	void ClipEnding()
	{
		clipping = false;
	}
private:
	double _scale;
	float _minDb;
	float _maxDb;
	float _mul;
	float _db;
	float _oldPeak;
	float _newPeak;
	obs_source_t *_source;
	obs_source_t *_parent;

	QMutex dataMutex;

	qreal minimumLevel;
	qreal warningLevel;
	qreal errorLevel;

	bool clipping = false;
	bool vertical = false;
	bool _mousePressed = false;

	QColor backgroundNominalColor;
	QColor backgroundWarningColor;
	QColor backgroundErrorColor;

	QColor foregroundNominalColor;
	QColor foregroundWarningColor;
	QColor foregroundErrorColor;

	QColor diffNominalColor;
	QColor diffWarningColor;
	QColor diffErrorColor;

	QColor magnitudeColor;

	QColor clipColor;

	int _channels = 2;
	int _handleWidth = 7;
	float currentMagnitude[MAX_AUDIO_CHANNELS];
	float currentPeak[MAX_AUDIO_CHANNELS];
	float currentInputPeak[MAX_AUDIO_CHANNELS];

	float displayMagnitude[MAX_AUDIO_CHANNELS];
	float displayPeak[MAX_AUDIO_CHANNELS];
	float displayInputPeak[MAX_AUDIO_CHANNELS];

	float displayPeakHold[MAX_AUDIO_CHANNELS];
	uint64_t displayPeakHoldLastUpdateTime[MAX_AUDIO_CHANNELS];
	float displayInputPeakHold[MAX_AUDIO_CHANNELS];
	uint64_t displayInputPeakHoldLastUpdateTime[MAX_AUDIO_CHANNELS];

	QTimer *redrawTimer;
	uint64_t lastRedrawTime = 0;
	uint64_t currentLastUpdateTime = 0;

	qreal peakDecayRate;
	qreal magnitudeIntegrationTime;
	qreal peakHoldDuration;
	qreal inputPeakHoldDuration;
	void paintInputMeter(QPainter &painter, int x, int y, int width,
		int height, float peakHold);
	void paintVMeter(QPainter &painter, int x, int y, int width,
		int height, float magnitude, float peak, float inputPeak,
		float peakHold, float inputPeakHold);
	void paintHMeter(QPainter &painter, int x, int y, int width,
		int height, float magnitude, float peak, float inputPeak,
		float peakHold, float inputPeakHold);

	inline void calculateBallistics(uint64_t ts,
		qreal timeSinceLastRedraw = 0.0);
	inline void calculateBallisticsForChannel(int channelNr,
		uint64_t ts, qreal timeSinceLastRedraw);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void dragMoveEvent(QDragMoveEvent *event) override;
	void dragLeaveEvent(QDragLeaveEvent *event) override;
	void dragEnterEvent(QDragEnterEvent *event) override;
public slots:
	void sample()
	{
		obs_properties_t *props = obs_source_properties(_source);
		struct gain_data* gf = static_cast<struct gain_data*>(
				obs_properties_get_param(props));

		if (gf) {
			if (pthread_mutex_trylock(&gf->mutex) == 0) {
				setChannels(gf->channels);
				setLevels(gf->mag, gf->peak, gf->input_peak);

				pthread_mutex_unlock(&gf->mutex);
			}
		} else {
			resetLevels();
		}
		obs_properties_destroy(props);
		QWidget::update();
	}
public:
	void resetLevels()
	{
		QMutexLocker locker(&dataMutex);
		//currentLastUpdateTime = 0;
		//lastRedrawTime = 0;
		for (int channelNr = 0; channelNr < MAX_AUDIO_CHANNELS; channelNr++) {
			currentMagnitude[channelNr] = -M_INFINITE;
			currentPeak[channelNr] = -M_INFINITE;
			currentInputPeak[channelNr] = -M_INFINITE;

			displayMagnitude[channelNr] = -M_INFINITE;
			displayPeak[channelNr] = -M_INFINITE;
			displayInputPeak[channelNr] = -M_INFINITE;
			displayPeakHoldLastUpdateTime[channelNr] = 0;

			displayPeakHold[channelNr] = -M_INFINITE;
			displayInputPeakHold[channelNr] = -M_INFINITE;

			displayInputPeakHoldLastUpdateTime[channelNr] = 0;
		}

		locker.unlock();
	}
	void setLevels(const float magnitude[MAX_AUDIO_CHANNELS],
		const float peak[MAX_AUDIO_CHANNELS],
		const float inputPeak[MAX_AUDIO_CHANNELS])
	{
		QMutexLocker locker(&dataMutex);

		for (int channelNr = 0; channelNr < MAX_AUDIO_CHANNELS; channelNr++) {
			currentMagnitude[channelNr] = mul_to_db(magnitude[channelNr]);
			currentPeak[channelNr] = mul_to_db(peak[channelNr]);
			currentInputPeak[channelNr] = mul_to_db(inputPeak[channelNr]);
		}

		// In case there are more updates then redraws we must make sure
		// that the ballistics of peak and hold are recalculated.
		locker.unlock();
	}

	void setChannels(int channels)
	{
		_channels = channels;
	}
	void setMagnitude(float magnitude, int channel)
	{
		currentMagnitude[channel] = magnitude;
	}
	void setPeak(float peak, int channel)
	{
		currentPeak[channel] = peak;
	}
	void setInputPeak(float peak, int channel)
	{
		currentInputPeak[channel] = peak;
	}
	double getMul()
	{
		return _mul;
	}
	double getDb()
	{
		return _db;
	}

	QGainWidget(QWidget* parent = Q_NULLPTR,
			Qt::WindowFlags f = Qt::WindowFlags(),
			obs_source_t *source = nullptr) :
			QWidget(parent, f)
	{
		_minDb = -30.0;
		_maxDb = 30.0;
		_source = source;
		_parent = obs_filter_get_parent(_source);

		backgroundNominalColor.setRgb(0x26, 0x7f, 0x26);    // Dark green
		backgroundWarningColor.setRgb(0x7f, 0x7f, 0x26);    // Dark yellow
		backgroundErrorColor.setRgb(0x7f, 0x26, 0x26);      // Dark red
		foregroundNominalColor.setRgb(0x4c, 0xff, 0x4c);    // Bright green
		foregroundWarningColor.setRgb(0xff, 0xff, 0x4c);    // Bright yellow
		foregroundErrorColor.setRgb(0xff, 0x4c, 0x4c);      // Bright red
		clipColor.setRgb(0xff, 0xff, 0xff);                 // Solid white

		diffNominalColor.setRed((backgroundNominalColor.red()
				+ foregroundNominalColor.red()) / 2);
		diffNominalColor.setGreen((backgroundNominalColor.green()
				+ foregroundNominalColor.green()) / 2);
		diffNominalColor.setBlue((backgroundNominalColor.blue()
				+ foregroundNominalColor.blue()) / 2);

		diffWarningColor.setRed((backgroundWarningColor.red()
			+ foregroundWarningColor.red()) / 2);
		diffWarningColor.setGreen((backgroundWarningColor.green()
			+ foregroundWarningColor.green()) / 2);
		diffWarningColor.setBlue((backgroundWarningColor.blue()
			+ foregroundWarningColor.blue()) / 2);

		diffErrorColor.setRed((backgroundErrorColor.red()
			+ foregroundErrorColor.red()) / 2);
		diffErrorColor.setGreen((backgroundErrorColor.green()
			+ foregroundErrorColor.green()) / 2);
		diffErrorColor.setBlue((backgroundErrorColor.blue()
			+ foregroundErrorColor.blue()) / 2);

		minimumLevel = -60.0;                               // -60 dB
		warningLevel = -20.0;                               // -20 dB
		errorLevel = -9.0;                                  //  -9 dB

		//peakDecayRate = 1.0;
		//peakDecayRate = 8.57;
		//peakDecayRate = 11.76;                              //  20 dB / 1.7 sec
		peakDecayRate = 25.53;
		magnitudeIntegrationTime = 0.3;                     //  99% in 300 ms
		peakHoldDuration = 5.0;                            //  20 seconds
		inputPeakHoldDuration = 5.0;                        //  20 seconds

		obs_data_t *settings = obs_source_get_settings(_source);
		_db = (double)obs_data_get_double(settings, "db");
		_mul = db_to_mul(_db);
		/*_minDb, _db ,_maxDb*/
		double diffDb = abs(_minDb - _maxDb);
		double relDb = _db - _minDb;
		_scale = relDb / diffDb;
		obs_data_release(settings);

		resetLevels();
		//sample();
		redrawTimer = new QTimer(this);
		QObject::connect(redrawTimer, &QTimer::timeout, this, &QGainWidget::sample);
		redrawTimer->start(34);
	};
	~QGainWidget()
	{
		if (redrawTimer)
			redrawTimer->deleteLater();
	};
};
