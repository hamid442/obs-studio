#include "caffeine-auth.h"

#include <obs.h>
#include <util/dstr.h>
#include <curl/curl.h>
#include <jansson.h>

#define do_log(level, format, ...) \
	blog(level, "[caffeine auth] " format, ##__VA_ARGS__)

#define log_error(format, ...)  do_log(LOG_ERROR, format, ##__VA_ARGS__)
#define log_warn(format, ...)  do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define log_info(format, ...)  do_log(LOG_INFO, format, ##__VA_ARGS__)
#define log_debug(format, ...)  do_log(LOG_DEBUG, format, ##__VA_ARGS__)

static size_t caffeine_signin_write_callback(char * ptr, size_t size,
	size_t nmemb, void * user_data)
{
	UNUSED_PARAMETER(size);
	if (nmemb == 0)
		return 0;

	struct dstr * result_str = user_data;
	dstr_ncat(result_str, ptr, nmemb);
	return nmemb;
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

	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, "X-Client-Type: obs");
	headers = curl_slist_append(headers, "X-Client-Version: " OBS_VERSION); /* TODO sanitize version */
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	struct dstr response_str;
	dstr_init(&response_str);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
		caffeine_signin_write_callback);
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
		dstr_copy(&response->credentials->access_token, access_token);
		dstr_copy(&response->credentials->caid, caid);
		dstr_copy(&response->credentials->refresh_token, refresh_token);
		dstr_copy(&response->credentials->credential, credential);
	}
	dstr_copy(&response->next, next);
	dstr_copy(&response->mfa_otp_method, mfa_otp_method);

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
		dstr_free(&auth_response->credentials->access_token);
		dstr_free(&auth_response->credentials->caid);
		dstr_free(&auth_response->credentials->refresh_token);
		dstr_free(&auth_response->credentials->credential);

		bfree(auth_response->credentials);
	}

	dstr_free(&auth_response->next);
	dstr_free(&auth_response->mfa_otp_method);

	bfree(auth_response);
}
