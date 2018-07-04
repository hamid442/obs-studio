#include <QCheckBox>
#include <QPixmap>

class QPaintEvernt;

class FiltersCheckBox : public QCheckBox {
	Q_OBJECT

	QPixmap checkedImage;
	QPixmap uncheckedImage;

public:
	FiltersCheckBox();

protected:
	void paintEvent(QPaintEvent *event) override;
};
