#include "auth-caffeine.hpp"

#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <qt-wrappers.hpp>
#include <obs-app.hpp>

#include "window-basic-main.hpp"
#include "remote-text.hpp"

#include <json11.hpp>

#include <ctime>

#include "ui-config.h"
#include "obf.h"

using namespace json11;

#include <browser-panel.hpp>
extern QCef *cef;
extern QCefCookieManager *panel_cookies;

/* ------------------------------------------------------------------------- */

#define CAFFEINE_AUTH_URL \
	"https://obsproject.com/app-auth/caffeine_post.php?action=redirect"
#define CAFFEINE_REDIRECT_URI \
	"https://obsproject.com/app-auth/caffeine_post.php"

static Auth::Def caffeineDef = {
	"Caffeine",
	Auth::Type::OAuth_StreamKey
};

/* ------------------------------------------------------------------------- */

CaffeineAuth::CaffeineAuth(const Def &d)
	: OAuthStreamKey(d)
{
}

bool CaffeineAuth::TokenExpired()
{
	if (token.empty())
		return true;
	if ((uint64_t)time(nullptr) > expire_time - 5)
		return true;
	return false;
}

bool CaffeineAuth::GetToken(const std::string &auth_code)
try {
	std::string output;
	std::string error;
	std::string desc;

	if (auth_code.empty() && !TokenExpired()) {
		return true;
	}

	std::string client_id = CAFFEINE_CLIENTID;
	std::string secret_id = CAFFEINE_SECRETID;
	deobfuscate_str(&client_id[0], CAFFEINE_HASH);
	deobfuscate_str(&secret_id[0], CAFFEINE_HASH);

	Json::object input_json {
		{"client_id", client_id},
		{"client_secret", secret_id},
	};

	if (!auth_code.empty()) {
		input_json["grant_type"] = "authorization_code";
		input_json["code"] = auth_code;
		input_json["redirect_uri"] = CAFFEINE_REDIRECT_URI;
	} else {
		input_json["grant_type"] = "refresh_token";
		input_json["refresh_token"] = refresh_token;
	}

	bool success = GetRemoteFileSafeBlock(
			"https://caffeine.tv/api/v1/oauth/token",
			output,
			error,
			nullptr,
			"application/json",
			Json(input_json).dump().c_str(),
			std::vector<std::string>(),
			nullptr,
			5);
	if (!success || output.empty())
		throw ErrorInfo("Failed to get token from remote", error);

	Json json = Json::parse(output, error);
	if (!error.empty())
		throw ErrorInfo("Failed to parse json", error);

	error = json["error"].string_value();
	if (!error.empty())
		throw ErrorInfo(error, json["error_description"].string_value());

	expire_time = (uint64_t)time(nullptr) + json["expires_in"].int_value();
	token       = json["access_token"].string_value();
	if (token.empty())
		throw ErrorInfo("Failed to get token from remote", error);

	if (!auth_code.empty()) {
		refresh_token = json["refresh_token"].string_value();
		if (refresh_token.empty())
			throw ErrorInfo("Failed to get refresh token from "
					"remote", error);
	}

	return true;

} catch (ErrorInfo info) {
	blog(LOG_WARNING, "%s: %s: %s",
			__FUNCTION__,
			info.message.c_str(),
			info.error.c_str());
	return false;
}

bool CaffeineAuth::GetChannelInfo()
try {
	if (!GetToken())
		return false;
	if (token.empty())
		return false;
	if (!key_.empty())
		return true;

	std::string auth;
	auth += "Authorization: Bearer ";
	auth += token;

	std::string client_id = CAFFEINE_CLIENTID;
	deobfuscate_str(&client_id[0], CAFFEINE_HASH);

	std::vector<std::string> headers;
	headers.push_back(std::string("Client-ID: ") + client_id);
	headers.push_back(std::move(auth));

	std::string output;
	std::string error;
	Json json;
	bool success;

	if (id.empty()) {
		success = GetRemoteFileSafeBlock(
				"https://caffeine.tv/api/v1/users/current",
				output,
				error,
				nullptr,
				"application/json",
				nullptr,
				headers,
				nullptr,
				5);
		if (!success || output.empty())
			throw ErrorInfo("Failed to get user info from remote",
					error);

		Json json = Json::parse(output, error);
		if (!error.empty())
			throw ErrorInfo("Failed to parse json", error);

		error = json["error"].string_value();
		if (!error.empty())
			throw ErrorInfo(error,
					json["error_description"].string_value());

		id    = std::to_string(json["channel"]["id"].int_value());
		name  = json["channel"]["token"].string_value();
	}

	/* ------------------ */

	std::string url;
	url += "https://caffeine.tv/api/v1/channels/";
	url += id;
	url += "/details";

	output.clear();

	success = GetRemoteFileSafeBlock(
			url.c_str(),
			output,
			error,
			nullptr,
			"application/json",
			nullptr,
			headers,
			nullptr,
			5);
	if (!success || output.empty())
		throw ErrorInfo("Failed to get stream key from remote", error);

	json = Json::parse(output, error);
	if (!error.empty())
		throw ErrorInfo("Failed to parse json", error);

	error = json["error"].string_value();
	if (!error.empty())
		throw ErrorInfo(error, json["error_description"].string_value());

	key_ = json["streamKey"].string_value();

	return true;
} catch (ErrorInfo info) {
	blog(LOG_WARNING, "%s: %s: %s",
			__FUNCTION__,
			info.message.c_str(),
			info.error.c_str());
	return false;
}

void CaffeineAuth::SaveInternal()
{
	OBSBasic *main = OBSBasic::Get();
	config_set_string(main->Config(), service(), "Name", name.c_str());
	config_set_string(main->Config(), service(), "Id", id.c_str());
	config_set_string(main->Config(), service(), "DockState",
			main->saveState().toBase64().constData());
	OAuthStreamKey::SaveInternal();
}

static inline std::string get_config_str(
		OBSBasic *main,
		const char *section,
		const char *name)
{
	const char *val = config_get_string(main->Config(), section, name);
	return val ? val : "";
}

bool CaffeineAuth::LoadInternal()
{
	OBSBasic *main = OBSBasic::Get();
	name = get_config_str(main, service(), "Name");
	id = get_config_str(main, service(), "Id");
	firstLoad = false;
	return OAuthStreamKey::LoadInternal();
}

class CaffeineChat : public QDockWidget {
public:
	inline CaffeineChat() : QDockWidget() {}

	QScopedPointer<QCefWidget> widget;
};
/*
static const char *ffz_script = "\
var ffz = document.createElement('script');\
ffz.setAttribute('src','https://cdn.frankerfacez.com/script/script.min.js');\
document.head.appendChild(ffz);";

static const char *bttv_script = "\
var bttv = document.createElement('script');\
bttv.setAttribute('src','https://cdn.betterttv.net/betterttv.js');\
document.head.appendChild(bttv);";
*/
void CaffeineAuth::LoadUI()
{
	if (uiLoaded)
		return;
	if (!GetChannelInfo())
		return;

	OBSBasic::InitBrowserPanelSafeBlock(false);
	OBSBasic *main = OBSBasic::Get();

	std::string url;
	url += "https://caffeine.tv/embed/chat/";
	url += id;

	chat.reset(new CaffeineChat());
	chat->setObjectName("caffeineChat");
	chat->resize(300, 600);
	chat->setMinimumSize(200, 300);
	chat->setWindowTitle(QTStr("CaffeineAuth.Chat"));
	chat->setAllowedAreas(Qt::AllDockWidgetAreas);

	QCefWidget *browser = cef->create_widget(nullptr, url, panel_cookies);
	chat->setWidget(browser);

	std::string script;

	browser->setStartupScript(script);

	main->addDockWidget(Qt::RightDockWidgetArea, chat.data());
	chatMenu.reset(main->AddDockWidgetMenu(chat.data()));

	chat->setFloating(true);
	if (firstLoad) {
		chat->setVisible(true);
	} else {
		const char *dockStateStr = config_get_string(main->Config(),
				service(), "DockState");
		QByteArray dockState =
			QByteArray::fromBase64(QByteArray(dockStateStr));
		main->restoreState(dockState);
	}

	uiLoaded = true;
}

std::shared_ptr<Auth> CaffeineAuth::Login(QWidget *parent)
{
	OAuthLogin login(parent, CAFFEINE_AUTH_URL, false);
	cef->add_popup_whitelist_url("about:blank", &login);

	if (login.exec() == QDialog::Rejected) {
		return nullptr;
	}

	std::shared_ptr<CaffeineAuth> auth = std::make_shared<CaffeineAuth>(caffeineDef);
	if (!auth->GetToken(QT_TO_UTF8(login.GetCode()))) {
		return nullptr;
	}

	std::string error;
	if (auth->GetChannelInfo()) {
		return auth;
	}

	return nullptr;
}

void CaffeineAuth::OnStreamConfig()
{
	OBSBasic *main = OBSBasic::Get();
	obs_service_t *service = main->GetService();

	obs_data_t *settings = obs_service_get_settings(service);

	obs_data_set_string(settings, "key", key().c_str());
	obs_service_update(service, settings);

	obs_data_release(settings);
}

static std::shared_ptr<Auth> CreateCaffeineAuth()
{
	return std::make_shared<CaffeineAuth>(caffeineDef);
}

static void DeleteCookies()
{
	if (panel_cookies) {
		panel_cookies->DeleteCookies("caffeine.tv", std::string());
	}
}

void RegisterCaffeineAuth()
{
	OAuth::RegisterOAuth(
			caffeineDef,
			CreateCaffeineAuth,
			CaffeineAuth::Login,
			DeleteCookies);
}
