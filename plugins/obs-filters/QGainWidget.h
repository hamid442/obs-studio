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
#include <QSlider>
#include <QSizePolicy>
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

class DoubleSlider : public QSlider {
	Q_OBJECT
private:
	double minVal, maxVal, minStep;
public:
	DoubleSlider(QWidget *parent = nullptr) : QSlider(parent)
	{
		connect(this, SIGNAL(valueChanged(int)),
			this, SLOT(intValChanged(int)));
	}

	void setDoubleConstraints(double newMin, double newMax,
		double newStep, double val)
	{
		minVal = newMin;
		maxVal = newMax;
		minStep = newStep;

		double total = maxVal - minVal;
		int intMax = int(total / minStep);

		setMinimum(0);
		setMaximum(intMax);
		setSingleStep(1);
		setDoubleVal(val);
	}
signals:
	void doubleValChanged(double val);

public slots:
	void intValChanged(int val)
	{
		emit doubleValChanged((minVal / minStep + val) * minStep);
	}
	void setDoubleVal(double val)
	{
		setValue(lround((val - minVal) / minStep));
	}
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

class OBSAudioMeter : public QWidget {
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

	Q_PROPERTY(QColor magnitudeColor
		READ getMagnitudeColor
		WRITE setMagnitudeColor DESIGNABLE true)

	Q_PROPERTY(QColor clipColor
		READ getClipColor
		WRITE setClipColor DESIGNABLE true)

	Q_PROPERTY(QColor majorTickColor
		READ getMajorTickColor
		WRITE setMajorTickColor DESIGNABLE true)
	Q_PROPERTY(QColor minorTickColor
		READ getMinorTickColor
		WRITE setMinorTickColor DESIGNABLE true)

	Q_PROPERTY(qreal clipLevel
		READ getClipLevel
		WRITE setClipLevel DESIGNABLE true)

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
	void ClipEnding()
	{
		clipping = false;
	}
public:
	enum tick_location {
		none,
		left,
		right,
		top = left,
		bottom = right
	};
private:
	bool vertical = false;
	bool clipping = false;
	tick_location _tick_opts;
	bool _tick_db;

	qreal minimumLevel;
	qreal warningLevel;
	qreal errorLevel;

	QColor backgroundNominalColor;
	QColor backgroundWarningColor;
	QColor backgroundErrorColor;

	QColor foregroundNominalColor;
	QColor foregroundWarningColor;
	QColor foregroundErrorColor;

	QColor magnitudeColor;

	QColor clipColor;

	QColor majorTickColor;
	QColor minorTickColor;

	QTimer *redrawTimer;
	uint64_t lastRedrawTime = 0;

	qreal peakDecayRate;
	qreal magnitudeIntegrationTime;
	qreal peakHoldDuration;
	qreal inputPeakHoldDuration;

	QFont tickFont;

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

	QPixmap *tickPaintCache = nullptr;

	int _channels = 2;

	obs_source_t *_source = nullptr;
	obs_source_t *_parent = nullptr;

	QMutex dataMutex;

	uint64_t currentLastUpdateTime = 0;

	inline bool detectIdle(uint64_t ts)
	{
		double timeSinceLastUpdate = (ts - currentLastUpdateTime) * 0.000000001;
		if (timeSinceLastUpdate > 0.5) {
			resetLevels();
			return true;
		} else {
			return false;
		}
	}

	inline void handleChannelCofigurationChange();

	void paintHTicks(QPainter &painter, int x, int y, int width,
		int height);
	void paintVTicks(QPainter &painter, int x, int y, int width,
		int height);

	inline void calculateBallistics(uint64_t ts,
		qreal timeSinceLastRedraw = 0.0);
	inline void calculateBallisticsForChannel(int channelNr,
		uint64_t ts, qreal timeSinceLastRedraw);

	void paintInputMeter(QPainter &painter, int x, int y, int width,
		int height, float peakHold);
	void paintVMeter(QPainter &painter, int x, int y, int width,
		int height, float magnitude, float peak, float inputPeak,
		float peakHold, float inputPeakHold);
	void paintHMeter(QPainter &painter, int x, int y, int width,
		int height, float magnitude, float peak, float inputPeak,
		float peakHold, float inputPeakHold);
protected:
	void paintEvent(QPaintEvent *event) override;
	void (*getSample)(OBSAudioMeter *meter) = nullptr;
public slots:
	void sample()
	{
		if(getSample != nullptr)
			getSample(this);
		QWidget::update();
	}
public:
	void setVertical()
	{
		vertical = true;
		handleChannelCofigurationChange();
	}
	void setHorizontal()
	{
		vertical = false;
		handleChannelCofigurationChange();
	}

	void setLayout(bool verticalLayout)
	{
		vertical = verticalLayout;
		handleChannelCofigurationChange();

	}
	void setTickOptions(tick_location location, bool show_db)
	{
		_tick_opts = location;
		_tick_db = show_db;
		handleChannelCofigurationChange();
	}
	template<class _Fn>
	void setCallback(_Fn _func)
	{
		getSample = _func;
	}
	const obs_source_t *getSource()
	{
		return _source;
	}
	void setChannels(int channels)
	{
		_channels = channels;
	}
	void removeCallback()
	{
		getSample = nullptr;
	}
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
		currentLastUpdateTime = os_gettime_ns();
		QMutexLocker locker(&dataMutex);
		
		for (int channelNr = 0; channelNr < MAX_AUDIO_CHANNELS; channelNr++) {
			currentMagnitude[channelNr] = mul_to_db(magnitude[channelNr]);
			currentPeak[channelNr] = mul_to_db(peak[channelNr]);
			currentInputPeak[channelNr] = mul_to_db(inputPeak[channelNr]);
		}

		// In case there are more updates then redraws we must make sure
		// that the ballistics of peak and hold are recalculated.
		locker.unlock();
		calculateBallistics(currentLastUpdateTime);
	}

	OBSAudioMeter(QWidget* parent = Q_NULLPTR,
		Qt::WindowFlags f = Qt::WindowFlags(),
		obs_source_t *source = nullptr) :
		QWidget(parent, f)
	{
		static int opt = 0;
		_tick_opts = (tick_location)opt;
		opt++;
		if (opt > right) {
			opt = 0;
		}
		_tick_db = true;
		vertical = true;
		tickFont = QFont("Arial");
		tickFont.setPixelSize(7);

		_source = source;
		_parent = obs_filter_get_parent(_source);

		backgroundNominalColor.setRgb(0x26, 0x7f, 0x26);    // Dark green
		backgroundWarningColor.setRgb(0x7f, 0x7f, 0x26);    // Dark yellow
		backgroundErrorColor.setRgb(0x7f, 0x26, 0x26);      // Dark red
		foregroundNominalColor.setRgb(0x4c, 0xff, 0x4c);    // Bright green
		foregroundWarningColor.setRgb(0xff, 0xff, 0x4c);    // Bright yellow
		foregroundErrorColor.setRgb(0xff, 0x4c, 0x4c);      // Bright red
		clipColor.setRgb(0xff, 0xff, 0xff);                 // Solid white

		majorTickColor.setRgb(0xff, 0xff, 0xff);            // Solid white
		minorTickColor.setRgb(0xcc, 0xcc, 0xcc);            // Black

		minimumLevel = -60.0;                               // -60 dB
		warningLevel = -20.0;                               // -20 dB
		errorLevel = -9.0;                                  //  -9 dB

		//peakDecayRate = 11.76;                              //  20 dB / 1.7 sec
		peakDecayRate = 25.53;
		magnitudeIntegrationTime = 0.3;                     //  99% in 300 ms
		peakHoldDuration = 5.0;                            //  20 seconds
		inputPeakHoldDuration = 5.0;                        //  20 seconds

		resetLevels();
		redrawTimer = new QTimer(this);
		QObject::connect(redrawTimer, &QTimer::timeout, this, &OBSAudioMeter::sample);
		redrawTimer->start(34);
	};
	~OBSAudioMeter()
	{
		if (redrawTimer)
			redrawTimer->deleteLater();
		removeCallback();
	};
};

class QGainWidget : public QWidget{

private:
	OBSAudioMeter *beforeMeter = nullptr;
	OBSAudioMeter *afterMeter = nullptr;
	double _scale;
	float _minDb;
	float _maxDb;
	float _mul;
	float _db;
	obs_source_t *_source;
	obs_source_t *_parent;

	bool vertical = false;
	bool _mousePressed = false;

	QMutex dataMutex;
	QTimer *redrawTimer;
protected:
	/*
	void paintEvent(QPaintEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void dragMoveEvent(QDragMoveEvent *event) override;
	void dragLeaveEvent(QDragLeaveEvent *event) override;
	void dragEnterEvent(QDragEnterEvent *event) override;
	*/
public slots:
	void updateDb(double db);
public:
	template<class _Fn>
	void setBeforeCallback(_Fn _func)
	{
		if (beforeMeter != nullptr)
			beforeMeter->setCallback(_func);
	}
	template<class _Fn>
	void setAfterCallback(_Fn _func)
	{
		if (afterMeter != nullptr)
			afterMeter->setCallback(_func);
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
		DoubleSlider *slider = new DoubleSlider();
		
		//QSlider *slider = new QSlider(vertical ? Qt::Vertical : Qt::Horizontal);
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
			//layout->addSpacerItem();
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
			blog(LOG_INFO, "[%i, %i]", beforeMeter->width(),
					beforeMeter->height());
		}
		connect(slider, SIGNAL(doubleValChanged(double)),
			this, SLOT(updateDb(double)));
			
		//connect(slider, SLOT(doubleValChanged()), 
		//resetLevels();
		//sample();
		//redrawTimer = new QTimer(this);
		//QObject::connect(redrawTimer, &QTimer::timeout, this, &QGainWidget::sample);
		//redrawTimer->start(34);
	};
	~QGainWidget()
	{
		//if (redrawTimer)
		//	redrawTimer->deleteLater();
	};
};
