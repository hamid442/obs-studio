#pragma once

#include <QMainWindow>
#include <QEvent>

#include <util/config-file.h>

class OBSMainWindow : public QMainWindow {
	Q_OBJECT

public:
	inline OBSMainWindow(QWidget *parent) : QMainWindow(parent) {}

	virtual config_t *Config() const=0;
	virtual void OBSInit()=0;

	virtual int GetProfilePath(char *path, size_t size, const char *file)
		const=0;
};

class QMidiEvent : public QEvent {
	std::vector<uint8_t> _message;
	double _deltaTime;
public:
	static const QEvent::Type midiType = static_cast<QEvent::Type>(QEvent::User + 0x4D49);
	QMidiEvent(std::vector<uint8_t> message, double deltatime) :
			QEvent(midiType)
	{
		_message = message;
		_deltaTime = deltatime;
	}
	std::vector<uint8_t> getMessage()
	{
		return _message;
	}
	double getDeltaTime()
	{
		return _deltaTime;
	}
};