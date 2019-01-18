#include "obs.h"
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
#include "OBSAudioMeter.h"
#include "../../../../UI/double-slider.hpp"
//#include "../../../../UI/properties-view.hpp"
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

class QGainWidget : public QWidget{
	Q_OBJECT
private:
	OBSAudioMeter *beforeMeter = nullptr;
	OBSAudioMeter *afterMeter = nullptr;
	DoubleSlider *slider = nullptr;
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
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	/*
	void paintEvent(QPaintEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
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
		obs_source_t *source = nullptr);

	~QGainWidget()
	{

	};
};
