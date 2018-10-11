#include "caffeine-api.h"

#include <obs.h>
#include <util/dstr.h>
#include <curl/curl.h>
#include <jansson.h>

#define do_log(level, format, ...) \
	blog(level, "[caffeine api] " format, ##__VA_ARGS__)

#define log_error(format, ...)  do_log(LOG_ERROR, format, ##__VA_ARGS__)
#define log_warn(format, ...)  do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define log_info(format, ...)  do_log(LOG_INFO, format, ##__VA_ARGS__)
#define log_debug(format, ...)  do_log(LOG_DEBUG, format, ##__VA_ARGS__)

static size_t caffeine_curl_write_callback(char * ptr, size_t size,
	size_t nmemb, void * user_data)
{
	UNUSED_PARAMETER(size);
	if (nmemb == 0)
		return 0;

	struct dstr * result_str = user_data;
	dstr_ncat(result_str, ptr, nmemb);
	return nmemb;
}

static struct curl_slist * caffeine_basic_headers() {
	struct curl_slist * headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, "X-Client-Type: obs");
	headers = curl_slist_append(headers, "X-Client-Version: " OBS_VERSION); /* TODO sanitize version */
	return headers;
}

static struct curl_slist * caffeine_authenticated_headers(
	struct caffeine_auth_info * auth_info)
{
	struct dstr header;
	dstr_init(&header);
	struct curl_slist * headers = caffeine_basic_headers();

	dstr_printf(&header, "Authorization: Bearer %s",
		auth_info->credentials->access_token);
	headers = curl_slist_append(headers, header.array);

	dstr_printf(&header, "X-Credential: %s",
		auth_info->credentials->credential);
	headers = curl_slist_append(headers, header.array);

	dstr_free(&header);
	return headers;
}

/* TODO: this is partly an exploration of both libcURL and libjansson; much of
 * this needs to be refactored, and will be reused for WebRTC signaling
 */
struct caffeine_auth_info * caffeine_signin(
	char const * username,
	char const * password,
	char const * otp)
{
	/* https://api.caffeine.tv/v1/account/signin */
	/* { "account": { "username": "foo", "password": "bar" } }*/
	/* TODO multifactor
	 * errors
	 * email verification
	 * etc this will be handled by the UI bits, service, etc*/
	struct caffeine_auth_info * response = NULL;

	json_t * request_json = NULL;
	if (otp)
		request_json = json_pack("{s:{s:s,s:s},s:{s:s}}",
			"account", "username", username, "password", password,
			"mfa", "otp", otp);
	else
		request_json = json_pack("{s:{s:s,s:s}}",
			"account", "username", username, "password", password);

	if (!request_json)
	{
		log_error("Failed to create request JSON");
		goto request_json_error;
	}

	char * request_body = json_dumps(request_json, 0);
	if (!request_body)
	{
		log_error("Failed to serialize request JSON");
		goto request_serialize_error;
	}

	CURL * curl = curl_easy_init();

	if (!curl)
	{
		log_error("Failed to initialize cURL");
		goto curl_init_error;
	}

	/* TODO: get urls from data ? */
	curl_easy_setopt(curl, CURLOPT_URL,
		"https://api.caffeine.tv/v1/account/signin");
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);

	struct curl_slist *headers = caffeine_basic_headers();
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	struct dstr response_str;
	dstr_init(&response_str);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
		caffeine_curl_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_str);

	char curl_error[CURL_ERROR_SIZE];
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		log_error("HTTP failure signing in: [%d] %s", res, curl_error);
		goto request_error;
	}

	json_error_t json_error;
	json_t * response_json = json_loads(response_str.array, 0, &json_error);
	if (!response_json) {
		log_error("Failed to parse signin response: %s",
			json_error.text);
		goto json_failed_error;
	}
	char const *error_text = NULL;
	int error_result = json_unpack(response_json, "{s:{s:[s!]}}",
		"errors", "_error", &error_text);
	if (error_result == 0) {
		log_error("Error logging in: %s", error_text);
		goto json_parsed_error;
	}

	char const *access_token = NULL;
	char const *refresh_token = NULL;
	char const *caid = NULL;
	char const *credential = NULL;
	char const *next = NULL;
	char const *mfa_otp_method = NULL;
	int success_result = json_unpack_ex(response_json, &json_error, 0,
		"{s?:{s:s,s:s,s:s,s:s}, s?:s, s?:s}",
		"credentials",
		"access_token", &access_token,
		"refresh_token", &refresh_token,
		"caid", &caid,
		"credential", &credential,

		"next", &next,

		"mfa_otp_method", &mfa_otp_method);

	if (success_result != 0) {
		log_error("Failed to extract auth info from signin result: [%s]",
			json_error.text);
		goto json_parsed_error;
	}

	response = bzalloc(sizeof(struct caffeine_auth_info));
	if (access_token) {
		response->credentials =
			bzalloc(sizeof(struct caffeine_credentials));
		response->credentials->access_token = bstrdup(access_token);
		response->credentials->caid = bstrdup(caid);
		response->credentials->refresh_token = bstrdup(refresh_token);
		response->credentials->credential = bstrdup(credential);
	}
	response->next = bstrdup(next);
	response->mfa_otp_method = bstrdup(mfa_otp_method);

json_parsed_error:
	json_decref(response_json);
json_failed_error:
request_error:
	dstr_free(&response_str);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
curl_init_error:
	free(request_body);
request_serialize_error:
	json_decref(request_json);
request_json_error:

	return response;
}

void caffeine_free_auth_info(struct caffeine_auth_info * auth_response)
{
	if (auth_response == NULL)
		return;

	if (auth_response->credentials)
	{
		bfree(auth_response->credentials->access_token);
		bfree(auth_response->credentials->caid);
		bfree(auth_response->credentials->refresh_token);
		bfree(auth_response->credentials->credential);
	}

	bfree(auth_response->credentials);
	bfree(auth_response->next);
	bfree(auth_response->mfa_otp_method);

	bfree(auth_response);
}

struct caffeine_user_info * caffeine_getuser(
	char const * caid,
	struct caffeine_auth_info * auth_info)
{
	if (caid == NULL) {
		log_error("Missing caid");
		return NULL;
	}

	if (auth_info == NULL || auth_info->credentials == NULL) {
		log_error("Missing auth");
		return NULL;
	}

	CURL * curl = curl_easy_init();

	if (!curl)
	{
		log_error("Failed to initialize cURL");
		return NULL;
	}

	struct caffeine_user_info * user_info = NULL;

	/* TODO: get urls from data ? */
	struct dstr url_str;
	dstr_init(&url_str);
	dstr_printf(&url_str, "https://api.caffeine.tv/v1/users/%s",
		auth_info->credentials->caid);

	curl_easy_setopt(curl, CURLOPT_URL, url_str.array);

	struct curl_slist *headers = caffeine_authenticated_headers(auth_info);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	struct dstr response_str;
	dstr_init(&response_str);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
		caffeine_curl_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_str);

	char curl_error[CURL_ERROR_SIZE];
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		log_error("HTTP failure fetching user: [%d] %s", res, curl_error);
		goto request_error;
	}

	json_error_t json_error;
	json_t * response_json = json_loads(response_str.array, 0, &json_error);
	if (!response_json) {
		log_error("Failed to parse user response: %s",
			json_error.text);
		goto json_failed_error;
	}
	char const *error_text = NULL;
	int error_result = json_unpack(response_json, "{s:{s:[s!]}}",
		"errors", "_error", &error_text);
	if (error_result == 0) {
		log_error("Error fetching user: %s", error_text);
		goto json_parsed_error;
	}

	char const *fetched_caid = NULL;
	char const *username = NULL;
	char const *stage_id = NULL;
	int can_broadcast = false; /* Using bool causes stack corruption on windows; I guess sizeof(bool) != sizeof(int) */
	int success_result = json_unpack_ex(response_json, &json_error, 0,
		"{s:{s:s,s:s,s:s,s:b}}",
		"user",
		"caid", &fetched_caid,
		"username", &username,
		"stage_id", &stage_id,
		"can_broadcast", &can_broadcast);

	if (success_result != 0) {
		log_error("Failed to extract user info from getuser result: [%s]",
			json_error.text);
		goto json_parsed_error;
	}

	if (strcmp(fetched_caid, caid) != 0) {
		log_warn("Somehow got a different user. Original caid: %s - Fetched caid: %s",
			caid, fetched_caid);
	}

	user_info = bzalloc(sizeof(struct caffeine_user_info));
	user_info->caid = bstrdup(caid);
	user_info->username = bstrdup(username);
	user_info->stage_id = bstrdup(stage_id);
	user_info->can_broadcast = can_broadcast;

json_parsed_error:
	json_decref(response_json);
json_failed_error:
request_error:
	dstr_free(&url_str);
	dstr_free(&response_str);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	return user_info;
}

void caffeine_free_user_info(struct caffeine_user_info * user_info)
{
	if (user_info == NULL)
		return;

	bfree(user_info->caid);
	bfree(user_info->username);
	bfree(user_info->stage_id);
}
