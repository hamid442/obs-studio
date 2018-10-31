#include <obs-module.h>

#include "caffeine-api.h"
#include "caffeine-service.h"

#define CAFFEINE_LOG_TITLE "caffeine service"
#include "caffeine-log.h"

#define OPT_USERNAME         "username"
#define OPT_PASSWORD         "password"
#define OPT_BROADCAST_TITLE  "broadcast_title"

struct caffeine_service {
	char * username;
	char * password;
	char * broadcast_title;
	struct caffeine_credentials * creds;
	struct caffeine_user_info * user_info;
};

static char const * caffeine_service_name(void * unused)
{
	UNUSED_PARAMETER(unused);

	return obs_module_text("CaffeineService");
}

static void caffeine_service_free_contents(struct caffeine_service * service)
{
	trace();
	if (!service)
		return;

	bfree(service->username);
	bfree(service->password);
	bfree(service->broadcast_title);
	caffeine_free_credentials(service->creds);
	caffeine_free_user_info(service->user_info);
	memset(service, 0, sizeof(struct caffeine_service));
}

static void caffeine_service_update(void * data, obs_data_t * settings)
{
	trace();
	struct caffeine_service * service = data;
	caffeine_service_free_contents(service);

	service->username =
		bstrdup(obs_data_get_string(settings, OPT_USERNAME));
	service->password =
		bstrdup(obs_data_get_string(settings, OPT_PASSWORD));
	service->broadcast_title =
		bstrdup(obs_data_get_string(settings, OPT_BROADCAST_TITLE));
	/* TODO: load auth tokens into caffeine auth info */
}

static void * caffeine_service_create(
	obs_data_t * settings,
	obs_service_t * unused)
{
	trace();
	UNUSED_PARAMETER(unused);

	struct caffeine_service * service =
		bzalloc(sizeof(struct caffeine_service));
	caffeine_service_update(service, settings);

	return service;
}

static void caffeine_service_destroy(void * data)
{
	trace();
	struct caffeine_service * service = data;
	caffeine_service_free_contents(service);
	bfree(service);
}

static obs_properties_t * caffeine_service_properties(void * data)
{
	trace();
	UNUSED_PARAMETER(data);
	obs_properties_t * properties = obs_properties_create();

	/* TODO: show different properties when already logged in */

	obs_properties_add_text(properties, OPT_USERNAME,
		obs_module_text("Username"), OBS_TEXT_DEFAULT);

	obs_property_t *password =
		obs_properties_add_text(properties, OPT_PASSWORD,
			obs_module_text("Password"), OBS_TEXT_PASSWORD);
	obs_property_set_transient(password, true);

	obs_properties_add_text(properties, OPT_BROADCAST_TITLE,
		obs_module_text("BroadcastTitle"), OBS_TEXT_DEFAULT);

	/* TODO: add button to do the signin here */

	return properties;
}

static void caffeine_service_defaults(obs_data_t *defaults)
{
	trace();
	obs_data_set_default_string(defaults, OPT_BROADCAST_TITLE,
		"LIVE on Caffeine!");
}

static bool caffeine_service_initialize(void * data, obs_output_t * output)
{
	trace();
	UNUSED_PARAMETER(output);
	struct caffeine_service * service = data;
	struct caffeine_auth_response * response = NULL;
	struct caffeine_user_info * user_info = NULL;

	if (service->creds)
		return true;

	bool result = false;

	/* TODO: make this asynchronous? */
	response = caffeine_signin(service->username, service->password, NULL);

	if (!response) {
		log_error("Failed login");
		return false;
	}
	if (response->next) {
		if (strcmp(response->next, "mfa_otp_required") == 0) {
			log_error("One time password NYI");
			goto cleanup_auth;
		}
		if (strcmp(response->next, "legal_acceptance_required")
			== 0) {
			log_error("Can't broadcast until terms of "
				"service are accepted");
			goto cleanup_auth;
		}
		if (strcmp(response->next, "email_verification") == 0) {
			log_error("Can't broadcast until email is "
				"verified");
			goto cleanup_auth;
		}
	}
	if (!response->credentials) {
		log_error("Empty auth response received");
		goto cleanup_auth;
	}

	user_info = caffeine_getuser(response->credentials);

	if (!user_info) {
		goto cleanup_auth;
	}
	if (!user_info->can_broadcast) {
		log_error("This user is not able to broadcast");
		caffeine_free_user_info(user_info);
		goto cleanup_auth;
	}

	/* Take ownership of creds
	 * TODO: make this interface cleaner
	 */
	service->creds = response->credentials;
	response->credentials = NULL;

	service->user_info = user_info;

	log_info("Successfully signed in");

	result = true;

cleanup_auth:
	caffeine_free_auth_response(response);
	return result;
}

static char const * caffeine_service_username(void * data)
{
	trace();
	struct caffeine_service * service = data;
	return service->username;
}

static char const * caffeine_service_password(void * data)
{
	trace();
	struct caffeine_service * service = data;
	return service->password;
}

static void * caffeine_service_query(void * data, int query_id, va_list unused)
{
	trace();
	UNUSED_PARAMETER(unused);
	struct caffeine_service * service = data;

	switch (query_id)
	{
	case CAFFEINE_QUERY_CREDENTIALS:
		return service->creds;
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
