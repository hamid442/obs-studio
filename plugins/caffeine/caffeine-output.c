#include <obs.h>
#include <obs-module.h>

#include <caffeine.h>

#include "caffeine-auth.h"


#define do_log(level, format, ...) \
	blog(level, "[caffeine output: '%s'] " format, \
			obs_output_get_name(stream->output), ##__VA_ARGS__)

#define log_error(format, ...)  do_log(LOG_ERROR, format, ##__VA_ARGS__)
#define log_warn(format, ...)  do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define log_info(format, ...)  do_log(LOG_INFO, format, ##__VA_ARGS__)
#define log_debug(format, ...)  do_log(LOG_DEBUG, format, ##__VA_ARGS__)

struct caffeine_output
{
	obs_output_t * output;
	caff_interface_handle interface;
	caff_broadcast_handle broadcast;
	struct caffeine_auth_response * auth_response; /* temporary */
};

static const char *caffeine_get_name(void *data)
{
	UNUSED_PARAMETER(data);

	return obs_module_text("CaffeineOutput"); /* TODO localize */
}

/* Converts caffeine-rtc (webrtc) log levels to OBS levels. NONE or unrecognized
 * values return 0 to indicate the message shouldn't be logged
 *
 * Note: webrtc uses INFO for debugging messages, not meant to be user-facing,
 * so this will never return LOG_INFO
 */
static int caffeine_to_obs_log_level(caff_log_severity severity)
{
	switch (severity)
	{
	case CAFF_LOG_SENSITIVE:
	case CAFF_LOG_VERBOSE:
	case CAFF_LOG_INFO:
		return LOG_DEBUG;
	case CAFF_LOG_WARNING:
		return LOG_WARNING;
	case CAFF_LOG_ERROR:
		return LOG_ERROR;
	case CAFF_LOG_NONE:
	default:
		return 0;
	}
}

static int caffeine_to_obs_error(caff_error error)
{
	switch (error)
	{
	default:
		return OBS_OUTPUT_ERROR;
	}
}

static void caffeine_log(caff_log_severity severity, char const * message)
{
	int log_level = caffeine_to_obs_log_level(severity);
	if (log_level)
		blog(log_level, "[caffeine-rtc] %s", message);
}

static void *caffeine_create(obs_data_t *settings, obs_output_t *output)
{
	UNUSED_PARAMETER(settings);

	struct caffeine_output *stream = bzalloc(sizeof(struct caffeine_output));
	stream->output = output;

	log_info("caffeine_create");

	stream->interface = caff_initialize(caffeine_log, CAFF_LOG_INFO);
	if (!stream->interface) {
		log_error("Unable to initialize Caffeine interface");
		bfree(stream);
		return NULL;
	}

	return stream;
}

static void caffeine_destroy(void *data)
{
	struct caffeine_output *stream = data;
	log_info("caffeine_destroy");
	caffeine_free_auth_response(stream->auth_response);
	caff_deinitialize(stream->interface);

	bfree(data);
}

static void caffeine_broadcast_started(void *data)
{
	struct caffeine_output *stream = data;
	log_info("caffeine_broadcast_started");

	obs_output_begin_data_capture(stream->output, 0);
}

static void caffeine_broadcast_failed(void *data, caff_error error)
{
	struct caffeine_output *stream = data;
	log_error("caffeine_broadcast_failed: %d", error);

	obs_output_signal_stop(stream->output, caffeine_to_obs_error(error));
}

static bool caffeine_start(void *data)
{
	struct caffeine_output *stream = data;
	log_info("caffeine_start");

	if (!obs_output_can_begin_data_capture(stream->output, 0))
		return false;

	/* TODO: do get the service, set up stream with broadcast name etc.
	 * Most of this work should be on separate thread
	 */

	/* WIP TODO Q&D FAKE */
	if (stream->auth_response == NULL) {
		char const * const username = "fakeuser";
		char const * const password = "fakepassword";
		char const * const otp = NULL;
		stream->auth_response = caffeine_signin(username, password, otp);
		if (!stream->auth_response) {
			log_error("Failed login");
			return false;
		}
		if (dstr_cmpi(&stream->auth_response->next,
			"mfa_otp_required") == 0) {
			log_error("One time password NYI");
			return false;
		}
		if (dstr_cmpi(&stream->auth_response->next,
			"legal_acceptance_required") == 0) {
			log_error("Can't broadcast until terms of service are accepted");
			return false;
		}
		if (dstr_cmpi(&stream->auth_response->next,
			"email_verification") == 0) {
			log_error("Can't broadcast until email is verified");
			return false;
		}
		if (!stream->auth_response->credentials) {
			log_error("Empty auth response received");
			return false;
		}
	}

	caff_broadcast_handle broadcast =
		caff_start_broadcast(stream->interface, stream,
			/* TODO caffeine_ice_gathered, */
			caffeine_broadcast_started, caffeine_broadcast_failed);

	if (!broadcast)
		return false;

	stream->broadcast = broadcast;
	return true;
}

static void caffeine_stop(void *data, uint64_t ts)
{
	UNUSED_PARAMETER(ts);

	struct caffeine_output *stream = data;
	log_info("caffeine_stop");

	/* TODO: teardown with service; do something with ts? */
	caff_end_broadcast(stream->broadcast);

	obs_output_end_data_capture(stream->output);
}

static void caffeine_raw_video(void *data, struct video_data *frame)
{
	UNUSED_PARAMETER(frame);

	struct caffeine_output *stream = data;

	/* TODO */
}

static void caffeine_raw_audio(void *data, struct audio_data *frames)
{
	UNUSED_PARAMETER(frames);

	struct caffeine_output *stream = data;

	/* TODO */
}

struct obs_output_info caffeine_output_info = {
	.id        = "caffeine_output",
	.flags     = OBS_OUTPUT_AV,  /* TODO: OBS_OUTPUT_SERVICE for login info, etc*/
	.get_name  = caffeine_get_name,
	.create    = caffeine_create,
	.destroy   = caffeine_destroy,
	.start     = caffeine_start,
	.stop      = caffeine_stop,
	.raw_video = caffeine_raw_video,
	.raw_audio = caffeine_raw_audio,
};
