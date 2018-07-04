#include <QPaintEvent>
#include <QPixmap>
#include <QPainter>
#include "filters-checkbox.hpp"

#include <util/c99defs.h>

FiltersCheckBox::FiltersCheckBox() : QCheckBox()
{
	setCheckable(false);
	
	checkedImage =
		QPixmap::fromImage(QImage(":/res/images/filters_mask.png"));
	uncheckedImage =
		QPixmap::fromImage(QImage(":/res/images/no_filters_mask.png"));
	setMinimumSize(16, 16);

	setStyleSheet("outline: none;");
}

void FiltersCheckBox::paintEvent(QPaintEvent *event)
{
	UNUSED_PARAMETER(event);

	QPixmap &pixmap = isChecked() ? checkedImage : uncheckedImage;
	QImage image(pixmap.size(), QImage::Format_ARGB32);

	QPainter draw(&image);
	draw.setCompositionMode(QPainter::CompositionMode_Source);
	draw.drawPixmap(0, 0, pixmap.width(), pixmap.height(), pixmap);
	draw.setCompositionMode(QPainter::CompositionMode_SourceIn);
	draw.fillRect(QRectF(QPointF(0.0f, 0.0f), pixmap.size()),
			palette().color(foregroundRole()));

	QPainter p(this);
	p.drawPixmap(0, 0, image.width(), image.height(),
			QPixmap::fromImage(image));
}
