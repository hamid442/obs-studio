#include <obs-module.h>

#include "caffeine-api.h"
#include "caffeine-service.h"

#define do_log(level, format, ...) \
	blog(level, "[caffeine service] " format, ##__VA_ARGS__)

#define log_error(format, ...)  do_log(LOG_ERROR, format, ##__VA_ARGS__)
#define log_warn(format, ...)  do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define log_info(format, ...)  do_log(LOG_INFO, format, ##__VA_ARGS__)
#define log_debug(format, ...)  do_log(LOG_DEBUG, format, ##__VA_ARGS__)

#define OPT_USERNAME         "username"
#define OPT_PASSWORD         "password"
#define OPT_BROADCAST_TITLE  "broadcast_title"

struct caffeine_service {
	char * username;
	char * password;
	char * broadcast_title;
	struct caffeine_auth_info * auth_info;
	struct caffeine_user_info * user_info;
};

static char const * caffeine_service_name(void * unused)
{
	UNUSED_PARAMETER(unused);

	return obs_module_text("CaffeineService");
}

static void caffeine_service_free_contents(struct caffeine_service * service)
{
	if (!service)
		return;

	bfree(service->username);
	bfree(service->password);
	bfree(service->broadcast_title);
	caffeine_free_auth_info(service->auth_info);
	caffeine_free_user_info(service->user_info);
	memset(service, 0, sizeof(struct caffeine_service));
}

static void caffeine_service_update(void * data, obs_data_t * settings)
{
	struct caffeine_service * service = data;
	caffeine_service_free_contents(service);

	service->username = bstrdup(obs_data_get_string(settings, OPT_USERNAME));
	service->password = bstrdup(obs_data_get_string(settings, OPT_PASSWORD));
	service->broadcast_title = bstrdup(obs_data_get_string(settings, OPT_BROADCAST_TITLE));
	/* TODO: load auth tokens into caffeine auth info */
}

static void * caffeine_service_create(
	obs_data_t * settings,
	obs_service_t * unused)
{
	log_info("caffeine_service_create");
	UNUSED_PARAMETER(unused);

	struct caffeine_service * service =
		bzalloc(sizeof(struct caffeine_service));
	caffeine_service_update(service, settings);

	return service;
}

static void caffeine_service_destroy(void * data)
{
	log_info("caffeine_service_destroy");
	struct caffeine_service * service = data;
	caffeine_service_free_contents(service);
	bfree(service);
}

static obs_properties_t * caffeine_service_properties(void * data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t * properties = obs_properties_create();

	/* TODO: show different properties when already logged in */

	obs_properties_add_text(properties, OPT_USERNAME,
		obs_module_text("Username"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(properties, OPT_PASSWORD,
		obs_module_text("Password"), OBS_TEXT_PASSWORD);
	obs_properties_add_text(properties, OPT_BROADCAST_TITLE,
		obs_module_text("BroadcastTitle"), OBS_TEXT_DEFAULT);

	/* TODO: add button to do the signin here */

	return properties;
}

static void caffeine_service_defaults(obs_data_t *defaults)
{
	obs_data_set_default_string(defaults, OPT_BROADCAST_TITLE, "LIVE on Caffeine!");
}

static bool caffeine_service_initialize(void * data, obs_output_t * output)
{
	UNUSED_PARAMETER(output);
	log_info("caffeine_service_initialize");
	struct caffeine_service * service = data;
	struct caffeine_auth_info * auth_info = NULL;
	struct caffeine_user_info * user_info = NULL;

	/* TODO: refresh tokens if necessary, do stuff asynchronously, etc */

	if (service->auth_info == NULL) {
		auth_info = caffeine_signin(
			service->username, service->password, NULL); 
		if (!auth_info) {
			log_error("Failed login");
			return false;
		}
		if (auth_info->next) {
			if (strcmp(auth_info->next, "mfa_otp_required") == 0) {
				log_error("One time password NYI");
				goto cleanup_auth;
			}
			if (strcmp(auth_info->next, "legal_acceptance_required")
				== 0) {
				log_error("Can't broadcast until terms of service are accepted");
				goto cleanup_auth;
			}
			if (strcmp(auth_info->next, "email_verification") == 0){
				log_error("Can't broadcast until email is verified");
				goto cleanup_auth;
			}
		}
		if (!auth_info->credentials) {
			log_error("Empty auth response received");
			goto cleanup_auth;
		}

		user_info = caffeine_getuser(auth_info->credentials->caid,
			auth_info);
		if (!user_info) {
			goto cleanup_auth;
		}
		if (!user_info->can_broadcast) {
			log_error("This user is not able to broadcast");
			caffeine_free_user_info(user_info);
			goto cleanup_user;
		}

		service->auth_info = auth_info;
		service->user_info = user_info;
	}

	return true;

cleanup_user:
	caffeine_free_user_info(user_info);
cleanup_auth:
	caffeine_free_auth_info(auth_info);
	return false;
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

static void * caffeine_service_query(void * data, int query_id, va_list unused)
{
	UNUSED_PARAMETER(unused);
	struct caffeine_service * service = data;
	log_info("caffeine_service_query");

	switch (query_id)
	{
	case CAFFEINE_QUERY_AUTH_INFO:
		return service->auth_info;
	case CAFFEINE_QUERY_STAGE_ID:
		return service->user_info->stage_id;
	case CAFFEINE_QUERY_BROADCAST_TITLE:
		return service->broadcast_title;
	default:
		log_warn("Unrecognized query [%d]", query_id);
		return NULL;
	}
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
	.get_defaults = caffeine_service_defaults,
	.initialize = caffeine_service_initialize,
	.get_username = caffeine_service_username,
	.get_password = caffeine_service_password,
	.query = caffeine_service_query,
	.get_output_type = caffeine_service_output_type,
};
