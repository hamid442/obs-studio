#include <obs-module.h>

#include "caffeine-auth.h"

#define do_log(level, format, ...) \
	blog(level, "[caffeine service] " format, ##__VA_ARGS__)

#define log_error(format, ...)  do_log(LOG_ERROR, format, ##__VA_ARGS__)
#define log_warn(format, ...)  do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define log_info(format, ...)  do_log(LOG_INFO, format, ##__VA_ARGS__)
#define log_debug(format, ...)  do_log(LOG_DEBUG, format, ##__VA_ARGS__)

struct caffeine_service {
	char * username;
	char * password;
	struct caffeine_auth_info * auth_info;
};

static char const * caffeine_service_name(void * unused)
{
	UNUSED_PARAMETER(unused);

	return "Caffeine.tv";
}

static void caffeine_service_free_contents(struct caffeine_service * service)
{
	bfree(service->username);
	bfree(service->password);
	caffeine_free_auth_info(service->auth_info);
}

static void caffeine_service_update(void * data, obs_data_t * settings)
{
	struct caffeine_service * service = data;
	caffeine_service_free_contents(service);

	service->username = bstrdup(obs_data_get_string(settings, "username"));
	service->password = bstrdup(obs_data_get_string(settings, "password"));
	/* TODO: load auth tokens into caffeine auth info */
}

static void * caffeine_service_create(
	obs_data_t * settings,
	obs_service_t * unused)
{
	UNUSED_PARAMETER(unused);

	struct caffeine_service * service =
		bzalloc(sizeof(struct caffeine_service));
	caffeine_service_update(service, settings);

	return service;
}

static void caffeine_service_destroy(void * data)
{
	struct caffeine_service * service = data;
	caffeine_service_free_contents(service);
	bfree(service);
}

static obs_properties_t * caffeine_service_properties(void * data)
{
	obs_properties_t * properties = obs_properties_create();

	/* TODO: show different properties when already logged in */

	obs_properties_add_text(properties, "username",
		obs_module_text("Username"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(properties, "password",
		obs_module_text("Password"), OBS_TEXT_PASSWORD);

	/* TODO: add button to do the signin here */

	return properties;
}

static bool caffeine_service_initialize(void * data, obs_output_t * output)
{
	struct caffeine_service * service = data;

	/* TODO: refresh tokens if necessary, do stuff asynchronously, etc */

	if (service->auth_info == NULL) {
		struct caffeine_auth_info * info = caffeine_signin(
			service->username, service->password, NULL); 
		if (!info) {
			log_error("Failed login");
			return false;
		}
		if (dstr_cmpi(&info->next,
			"mfa_otp_required") == 0) {
			log_error("One time password NYI");
			return false;
		}
		if (dstr_cmpi(&info->next,
			"legal_acceptance_required") == 0) {
			log_error("Can't broadcast until terms of service are accepted");
			return false;
		}
		if (dstr_cmpi(&info->next,
			"email_verification") == 0) {
			log_error("Can't broadcast until email is verified");
			return false;
		}
		if (!info->credentials) {
			log_error("Empty auth response received");
			return false;
		}
		service->auth_info = info;
	}

	return true;
}

static char const * caffeine_service_username(void * data)
{
	struct caffeine_service * service = data;
	return service->username;
}

static char const * caffeine_service_password(void * data)
{
	struct caffeine_service * service = data;
	return service->password;
}

static char const * caffeine_service_output_type(void * unused)
{
	UNUSED_PARAMETER(unused);
	return "caffeine_output";
}

struct obs_service_info caffeine_service_info = {
	.id = "caffeine_service",
	.get_name = caffeine_service_name,
	.create = caffeine_service_create,
	.destroy = caffeine_service_destroy,
	.update = caffeine_service_update,
	.get_properties = caffeine_service_properties,
	.initialize = caffeine_service_initialize,
	.get_username = caffeine_service_username,
	.get_password = caffeine_service_password,
	.get_output_type = caffeine_service_output_type,
};
