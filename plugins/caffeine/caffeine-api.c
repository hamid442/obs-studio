#include "caffeine-api.h"

#include <obs.h>

#include <curl/curl.h>
#include <jansson.h>
#include <util/dstr.h>
#include <util/threading.h>

#define CAFFEINE_LOG_TITLE "caffeine api"
#include "caffeine-log.h"

/* TODO: load these from config? */
#define API_ENDPOINT       "https://api.caffeine.tv/"
#define REALTIME_ENDPOINT  "https://realtime.caffeine.tv/"

/* TODO: some of these are deprecated */
#define SIGNIN_URL         API_ENDPOINT "v1/account/signin"
#define REFRESH_TOKEN_URL  API_ENDPOINT "v1/account/token"
#define GETUSER_URL_F      API_ENDPOINT "v1/users/%s"
#define STREAM_URL         API_ENDPOINT "v2/broadcasts/streams"
#define TRICKLE_URL_F      API_ENDPOINT "v2/broadcasts/streams/%s"
#define BROADCAST_URL      API_ENDPOINT "v1/broadcasts"
#define HEARTBEAT_URL_F    API_ENDPOINT "v2/broadcasts/streams/%s/heartbeat"

#define STAGE_STATE_URL_F  REALTIME_ENDPOINT "v2/stages/%s/details"

struct caffeine_credentials {
	char * access_token;
	char * refresh_token;
	char * caid;
	char * credential;

	pthread_mutex_t mutex;
};

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
	headers = curl_slist_append(headers, "X-Client-Type: obs");
	headers = curl_slist_append(headers, "X-Client-Version: " OBS_VERSION);
	return headers;
}

static struct curl_slist * caffeine_authenticated_headers(
	struct caffeine_credentials * creds)
{
	pthread_mutex_lock(&creds->mutex);
	struct dstr header;
	dstr_init(&header);
	struct curl_slist * headers = caffeine_basic_headers();

	dstr_printf(&header, "Authorization: Bearer %s", creds->access_token);
	headers = curl_slist_append(headers, header.array);

	dstr_printf(&header, "X-Credential: %s", creds->credential);
	headers = curl_slist_append(headers, header.array);

	dstr_free(&header);
	pthread_mutex_unlock(&creds->mutex);
	return headers;
}

static struct caffeine_credentials * make_credentials(
	char const * access_token,
	char const * caid,
	char const * refresh_token,
	char const * credential)
{
	struct caffeine_credentials * creds =
		bzalloc(sizeof(struct caffeine_credentials));

	creds->access_token = bstrdup(access_token);
	creds->caid = bstrdup(caid);
	creds->refresh_token = bstrdup(refresh_token);
	creds->credential = bstrdup(credential);
	pthread_mutex_init(&creds->mutex, NULL);

	return creds;
}

char const * caffeine_refresh_token(struct caffeine_credentials * creds)
{
	return creds->refresh_token;
}

/* TODO: refactor this - lots of dupe code between request types
 * TODO: handle retries, auth token refresh, etc
 */
struct caffeine_auth_response * caffeine_signin(
	char const * username,
	char const * password,
	char const * otp)
{
	trace();
	struct caffeine_auth_response * response = NULL;

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

	curl_easy_setopt(curl, CURLOPT_URL, SIGNIN_URL);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);

	struct curl_slist *headers = caffeine_basic_headers();
	headers = curl_slist_append(headers, "Content-Type: application/json");
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
		log_error("Failed to extract response info: [%s]",
			json_error.text);
		goto json_parsed_error;
	}

	struct caffeine_credentials * creds = NULL;

	if (access_token) {
		log_debug("Sign-in complete");
		creds = make_credentials(access_token, caid, refresh_token, credential);
	}
	else if (mfa_otp_method) {
		log_debug("MFA required");
	}
	else
		log_error("Sign-in response missing");

	response = bzalloc(sizeof(struct caffeine_auth_response));

	response->credentials = creds;
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

struct caffeine_credentials * caffeine_refresh_auth(
	char const * refresh_token)
{
	trace();

	json_t * request_json = json_pack("{s:s}",
		"refresh_token", refresh_token);

	struct caffeine_credentials * result = NULL;

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

	curl_easy_setopt(curl, CURLOPT_URL, REFRESH_TOKEN_URL);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);

	struct curl_slist *headers = caffeine_basic_headers();
	headers = curl_slist_append(headers, "Content-Type: application/json");
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
		log_error("HTTP failure refreshing tokens: [%d] %s", res, curl_error);
		goto request_error;
	}

	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	log_debug("Http response [%ld]", response_code);

	json_error_t json_error;
	json_t * response_json = json_loads(response_str.array, 0, &json_error);
	if (!response_json) {
		log_error("Failed to parse refresh response: %s",
			json_error.text);
		goto json_failed_error;
	}
	char const *error_text = NULL;
	int error_result = json_unpack(response_json, "{s:{s:[s!]}}",
		"errors", "_error", &error_text);
	if (error_result == 0) {
		log_error("Error refreshing tokens: %s", error_text);
		goto json_parsed_error;
	}

	char const *access_token = NULL;
	char const *new_refresh_token = NULL;
	char const *caid = NULL;
	char const *credential = NULL;
	int success_result = json_unpack_ex(response_json, &json_error, 0,
		"{s:{s:s,s:s,s:s,s:s}}",
		"credentials",
		"access_token", &access_token,
		"refresh_token", &new_refresh_token,
		"caid", &caid,
		"credential", &credential);

	if (success_result != 0) {
		log_error("Failed to extract response info: [%s]",
			json_error.text);
		goto json_parsed_error;
	}

	result = make_credentials(access_token, caid, new_refresh_token,
		credential);

	log_debug("Auth tokens refreshed");

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

	return result;
}

void caffeine_free_credentials(struct caffeine_credentials * credentials)
{
	trace();
	if (!credentials)
		return;
	pthread_mutex_lock(&credentials->mutex);
	bfree(credentials->access_token);
	bfree(credentials->caid);
	bfree(credentials->refresh_token);
	bfree(credentials->credential);
	credentials->access_token = NULL;
	credentials->caid = NULL;
	credentials->refresh_token = NULL;
	credentials->credential = NULL;
	pthread_mutex_unlock(&credentials->mutex);
	pthread_mutex_destroy(&credentials->mutex);
	bfree(credentials);
}

void caffeine_free_auth_response(struct caffeine_auth_response * auth_response)
{
	trace();
	if (auth_response == NULL)
		return;

	caffeine_free_credentials(auth_response->credentials);

	bfree(auth_response->next);
	bfree(auth_response->mfa_otp_method);

	bfree(auth_response);
}

struct caffeine_user_info * caffeine_getuser(
	struct caffeine_credentials * creds)
{
	trace();
	if (creds == NULL) {
		log_error("Missing credentials");
		return NULL;
	}

	CURL * curl = curl_easy_init();

	if (!curl)
	{
		log_error("Failed to initialize cURL");
		return NULL;
	}

	struct caffeine_user_info * user_info = NULL;

	struct dstr url_str;
	dstr_init(&url_str);
	dstr_printf(&url_str, GETUSER_URL_F, creds->caid);

	curl_easy_setopt(curl, CURLOPT_URL, url_str.array);

	struct curl_slist *headers = caffeine_authenticated_headers(creds);
	headers = curl_slist_append(headers, "Content-Type: application/json");
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
	int can_broadcast = false; /* Jansson uses int for bool */
	int success_result = json_unpack_ex(response_json, &json_error, 0,
		"{s:{s:s,s:s,s:s,s:b}}",
		"user",
		"caid", &fetched_caid,
		"username", &username,
		"stage_id", &stage_id,
		"can_broadcast", &can_broadcast);

	if (success_result != 0) {
		log_error("Failed to extract response info: [%s]",
			json_error.text);
		goto json_parsed_error;
	}

	if (strcmp(fetched_caid, creds->caid) != 0) {
		log_warn("Somehow got a different user. Original caid: %s - "
			 "Fetched caid: %s",
			 creds->caid, fetched_caid);
	}

	user_info = bzalloc(sizeof(struct caffeine_user_info));
	user_info->caid = bstrdup(fetched_caid);
	user_info->username = bstrdup(username);
	user_info->stage_id = bstrdup(stage_id);
	user_info->can_broadcast = can_broadcast;

	log_debug("Got user details");

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
	trace();
	if (user_info == NULL)
		return;

	bfree(user_info->caid);
	bfree(user_info->username);
	bfree(user_info->stage_id);
}

struct caffeine_stream_info * caffeine_start_stream(
	char const * stage_id,
	char const * sdp_offer,
	struct caffeine_credentials * creds)
{
	trace();
	struct caffeine_stream_info * response = NULL;

	json_t * request_json = NULL;
	request_json = json_pack("{s:s,s:s,s:s}",
		"stage_id", stage_id,
		"offer", sdp_offer,
		"video_codec", "h264");

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

	curl_easy_setopt(curl, CURLOPT_URL, STREAM_URL);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);

	struct curl_slist *headers = caffeine_authenticated_headers(creds);
	headers = curl_slist_append(headers, "Content-Type: application/json");
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
		log_error("HTTP failure starting stream: [%d] %s",
			res, curl_error);
		goto request_error;
	}

	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

	json_error_t json_error;
	json_t * response_json = json_loads(response_str.array, 0, &json_error);
	if (!response_json) {
		log_error("Failed to parse start stream response: %s",
			json_error.text);
		goto json_failed_error;
	}
	char const *error_text = NULL;
	int error_result = json_unpack(response_json, "{s:[s!]}",
		"_errors", &error_text);
	if (error_result == 0) {
		log_error("Error starting stream: %s", error_text);
		goto json_parsed_error;
	}

	char const *stream_id = NULL;
	char const *sdp_answer = NULL;
	char const *signed_payload = NULL;
	int success_result = json_unpack_ex(response_json, &json_error, 0,
		"{s:s,s:s,s:s}",
		"id", &stream_id,
		"answer", &sdp_answer,
		"signed_payload", &signed_payload);

	if (success_result != 0) {
		log_error("Failed to extract response info: [%s]",
			json_error.text);
		goto json_parsed_error;
	}

	response = bzalloc(sizeof(struct caffeine_stream_info));
	response->stream_id = bstrdup(stream_id);
	response->sdp_answer = bstrdup(sdp_answer);
	response->signed_payload = bstrdup(signed_payload);

	log_debug("Stream started");

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

void caffeine_free_stream_info(struct caffeine_stream_info * stream_info)
{
	trace();
	if (!stream_info)
		return;

	bfree(stream_info->stream_id);
	bfree(stream_info->sdp_answer);
	bfree(stream_info->signed_payload);
}

bool caffeine_trickle_candidates(
	caff_ice_candidates candidates,
	size_t num_candidates,
	struct caffeine_credentials * creds,
	struct caffeine_stream_info const * stream_info)
{
	trace();
	json_t * ice_candidates = json_array();
	for (size_t i = 0; i < num_candidates; ++i)
	{
		json_array_append_new(ice_candidates, json_pack(
			"{s:s,s:s,s:i}",
			"candidate", candidates[i].sdp,
			"sdpMid", candidates[i].sdp_mid,
			"sdpMLineIndex", candidates[i].sdp_mline_index));
	}

	json_t * request_json = json_pack(
		"{s:o,s:s}",
		"ice_candidates", ice_candidates,
		"signed_payload", stream_info->signed_payload);

	bool response = false;

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

	struct dstr url_str;
	dstr_init(&url_str);
	dstr_printf(&url_str, TRICKLE_URL_F, stream_info->stream_id);

	curl_easy_setopt(curl, CURLOPT_URL, url_str.array);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");

	struct curl_slist *headers = caffeine_authenticated_headers(creds);
	headers = curl_slist_append(headers, "Content-Type: application/json");
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
		log_error("HTTP failure negotiating ICE: [%d] %s",
			res, curl_error);
		goto request_error;
	}

	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

	json_error_t json_error;
	json_t * response_json = json_loads(response_str.array, 0, &json_error);
	if (!response_json) {
		log_error("Failed to parse ICE response: %s",
			json_error.text);
		goto json_failed_error;
	}
	char const *error_text = NULL;
	int error_result = json_unpack(response_json, "{s:[s!]}",
		"_errors", &error_text);
	if (error_result == 0) {
		log_error("Error negotiating ICE: %s", error_text);
		goto json_parsed_error;
	}

	char const *stream_id = NULL;
	char const *stage_id = NULL;
	char const *new_signed_payload = NULL;
	int success_result = json_unpack_ex(response_json, &json_error, 0,
		"{s:s,s:s,s:s}",
		"id", &stream_id,
		"stage_id", &stage_id,
		"signed_payload", &new_signed_payload);

	if (success_result != 0) {
		log_error("Failed to extract response info: [%s]",
			json_error.text);
		goto json_parsed_error;
	}

	response = true;
	log_debug("ICE candidates trickled");

json_parsed_error:
	json_decref(response_json);
json_failed_error:
request_error:
	dstr_free(&response_str);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	dstr_free(&url_str);
curl_init_error:
	free(request_body);
request_serialize_error:
	json_decref(request_json);
request_json_error:

	return response;
}

bool refresh_credentials(struct caffeine_credentials * creds)
{
	trace();
	char * refresh_token = NULL;
	pthread_mutex_lock(&creds->mutex);
	refresh_token = bstrdup(creds->refresh_token);
	pthread_mutex_unlock(&creds->mutex);

	struct caffeine_credentials * new_creds =
		caffeine_refresh_auth(refresh_token);

	if (!new_creds)
		return false;

	pthread_mutex_lock(&creds->mutex);

	bfree(creds->access_token);
	bfree(creds->caid);
	bfree(creds->refresh_token);
	bfree(creds->credential);

	creds->access_token = new_creds->access_token;
	creds->caid = new_creds->caid;
	creds->refresh_token = new_creds->refresh_token;
	creds->credential = new_creds->credential;

	new_creds->access_token = NULL;
	new_creds->caid = NULL;
	new_creds->refresh_token = NULL;
	new_creds->credential = NULL;

	caffeine_free_credentials(new_creds);

	pthread_mutex_unlock(&creds->mutex);

	return true;
}

bool send_heartbeat(
	char const * stage_id,
	char const * signed_payload,
	struct caffeine_credentials * creds)
{
	trace();
	bool result = false;

	json_t * request_json =
		json_pack("{s:s}", "signed_payload", signed_payload);

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

	struct dstr url_str;
	dstr_init(&url_str);
	dstr_printf(&url_str, HEARTBEAT_URL_F, stage_id);

	curl_easy_setopt(curl, CURLOPT_URL, url_str.array);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);

	struct curl_slist *headers = caffeine_authenticated_headers(creds);
	headers = curl_slist_append(headers, "Content-Type: application/json");
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
		log_error("HTTP failure going live: [%d] %s",
			res, curl_error);
		goto request_error;
	}

	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

	/* TODO: implement this more generally (along with retries) */
	if (response_code == 401) {
		log_info("Unauthorized - refreshing credentials");
		refresh_credentials(creds);
		result = send_heartbeat(stage_id, signed_payload, creds);
		goto auth_refresh;
	}

	log_debug("Http response code [%ld]", response_code);

	json_error_t json_error;
	json_t * response_json = json_loads(response_str.array, 0, &json_error);
	if (!response_json) {
		log_error("Failed to parse heartbeat response: %s",
			json_error.text);
		goto json_failed_error;
	}
	char const *error_text = NULL;
	int error_result = json_unpack(response_json, "{s:[s!]}",
		"_errors", &error_text);
	if (error_result == 0) {
		log_error("Error sending heartbeat: %s", error_text);
		goto json_parsed_error;
	}

	if (response_code / 100 == 2) {
		log_debug("Heartbeat sent");
		result = true;
	}

json_parsed_error:
	json_decref(response_json);
json_failed_error:
request_error:
auth_refresh:
	dstr_free(&response_str);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	dstr_free(&url_str);
curl_init_error:
	free(request_body);
request_serialize_error:
	json_decref(request_json);
request_json_error:

	return result;
}


/* TODO: Here be dragons
 *
 * Stuff below here likely won't stay
 */

char * set_stage_live(
	bool isLive,
	char const * session_id,
	char const * stage_id,
	char const * stream_id,
	char const * title,
	struct caffeine_credentials * creds)
{
	trace();
	json_t * stream_json = json_pack("{s:s,s:s,s:s,s:{s:b,s:b}}",
		"id", stream_id,
		"label", "game",
		"type", "primary",
		"capabilities",
		"audio", 1,
		"video", 1);

	if (!stream_json)
		return NULL;

	char * result = NULL;

	json_t * payload_json = json_pack("{s:s,s:s,s:s,s:[o],s:s}",
		"state", isLive ? "ONLINE" : "OFFLINE",
		"title", title,
		"game_id", "79",
		"streams", stream_json,
		"host_connection_quality", "GOOD");

	if (!payload_json)
	{
		log_error("Failed to create request JSON");
		json_decref(stream_json);
		return NULL;
	}

	char const * request_type = "POST";
	if (session_id) {
		json_object_set_new(payload_json,
			"session_id", json_string(session_id));
		request_type = "PUT";
	}

	json_t * request_json = json_pack("{s:o}", "v2", payload_json);
	if (!request_json)
	{
		log_error("Failed to create request JSON");
		json_decref(payload_json);
		return NULL;
	}

	char * request_body = json_dumps(request_json, 0);
	json_decref(request_json);
	if (!request_body)
	{
		log_error("Failed to serialize request JSON");
		return NULL;
	}

	CURL * curl = curl_easy_init();
	if (!curl)
	{
		log_error("Failed to initialize cURL");
		goto curl_init_error;
	}

	struct dstr url_str;
	dstr_init(&url_str);
	dstr_printf(&url_str, STAGE_STATE_URL_F, stage_id);

	curl_easy_setopt(curl, CURLOPT_URL, url_str.array);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request_type);

	struct curl_slist *headers = caffeine_authenticated_headers(creds);
	headers = curl_slist_append(headers, "Content-Type: application/json");
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
		log_error("HTTP failure going live: [%d] %s",
			res, curl_error);
		goto request_error;
	}

	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	log_debug("Http response [%ld]", response_code);

	json_error_t json_error;
	json_t * response_json = json_loads(response_str.array, 0, &json_error);
	if (!response_json) {
		log_error("Failed to stage state response: %s",
			json_error.text);
		goto json_failed_error;
	}
	char const *error_text = NULL;
	int error_result = json_unpack(response_json, "{s:[s!]}",
		"_errors", &error_text);
	if (error_result == 0) {
		log_error("Error updating stage state: %s", error_text);
		goto json_parsed_error;
	}

	if (!session_id) {
		char const * new_session_id;
		if (json_unpack(response_json, "{s:s}",
			"session_id", &new_session_id) != 0) {
			goto request_error;
		}
		log_debug("got session id %s", new_session_id);

		result = bstrdup(new_session_id);
	}

	if (response_code / 100 == 2)
		log_debug("Stage set %s", (isLive ? "live" : "offline"));
	else
		log_warn("Failed to set stage state");

json_parsed_error:
	json_decref(response_json);
json_failed_error:
request_error:
	dstr_free(&response_str);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	dstr_free(&url_str);
curl_init_error:
	free(request_body);

	return result;
}

void add_text_part(
	struct curl_httppost ** post,
	struct curl_httppost ** last,
	char const * name,
	char const * text)
{
	curl_formadd(post, last,
		CURLFORM_COPYNAME, name,
		CURLFORM_COPYCONTENTS, text,
		CURLFORM_END);
}

bool create_broadcast(
	char const * title,
	struct caffeine_credentials * creds)
{
	trace();
	CURL * curl = curl_easy_init();

	if (!curl)
	{
		log_error("Failed to initialize cURL");
		return false;
	}

	struct curl_httppost * post = NULL;
	struct curl_httppost * last = NULL;

	add_text_part(&post, &last, "broadcast[name]", title);
	add_text_part(&post, &last, "broadcast[description]", "");
	add_text_part(&post, &last, "broadcast[content_rating]", "PG");
	add_text_part(&post, &last, "broadcast[platform]", "PC");
	add_text_part(&post, &last, "broadcast[state]", "ONLINE");
	add_text_part(&post, &last, "broadcast[game_id]", "79");

	curl_easy_setopt(curl, CURLOPT_HTTPPOST, post);

	curl_easy_setopt(curl, CURLOPT_URL, BROADCAST_URL);

	struct curl_slist *headers = caffeine_authenticated_headers(creds);
	headers = curl_slist_append(headers, "Content-Type: multipart/form-data");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	struct dstr response_str;
	dstr_init(&response_str);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
		caffeine_curl_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_str);

	char curl_error[CURL_ERROR_SIZE];
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);

	bool result = false;

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		log_error("HTTP failure going live: [%d] %s",
			res, curl_error);
		goto request_error;
	}

	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

	log_debug("Http response code [%ld]", response_code);

	json_error_t json_error;
	json_t * response_json = json_loads(response_str.array, 0, &json_error);
	if (!response_json) {
		log_error("Failed to stage state response: %s",
			json_error.text);
		goto json_failed_error;
	}
	char const *error_text = NULL;
	int error_result = json_unpack(response_json, "{s:[s!]}",
		"_errors", &error_text);
	if (error_result == 0) {
		log_error("Error updating stage state: %s", error_text);
		goto json_parsed_error;
	}

	if (response_code / 100 == 2) {
		log_debug("Stage state updated");
		result = true;
	}
	else {
		log_error("Failed to update stage state");
	}

json_parsed_error:
	json_decref(response_json);
json_failed_error:
request_error:
	dstr_free(&response_str);
	curl_slist_free_all(headers);
	curl_formfree(post);
	curl_easy_cleanup(curl);

	return result;
}
