#pragma once

#include "auth-oauth.hpp"

class CaffeineChat;

class CaffeineAuth : public OAuthStreamKey {
	Q_OBJECT

	QSharedPointer<CaffeineChat> chat;
	QSharedPointer<QAction> chatMenu;
	bool uiLoaded = false;

	std::string name;
	std::string id;

	bool TokenExpired();
	bool GetToken(const std::string &auth_code = std::string());

	virtual void SaveInternal() override;
	virtual bool LoadInternal() override;

	bool GetChannelInfo();

	virtual void LoadUI() override;
	virtual void OnStreamConfig() override;

public:
	CaffeineAuth(const Def &d);

	static std::shared_ptr<Auth> Login(QWidget *parent);
};
