#include <obs-module.h>

#include <util/platform.h>
#include <util/threading.h>

#include <caffeine.h>

#include "caffeine-api.h"
#include "caffeine-service.h"


#define do_log(level, format, ...) \
	blog(level, "[caffeine output: '%s'] " format, \
			obs_output_get_name(context->output), ##__VA_ARGS__)

#define log_error(format, ...)  do_log(LOG_ERROR, format, ##__VA_ARGS__)
#define log_warn(format, ...)  do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define log_info(format, ...)  do_log(LOG_INFO, format, ##__VA_ARGS__)
#define log_debug(format, ...)  do_log(LOG_DEBUG, format, ##__VA_ARGS__)

struct caffeine_output
{
	obs_output_t * output;
	caff_interface_handle interface;
	caff_stream_handle stream;
	struct caffeine_stream_info * stream_info;
	pthread_mutex_t stream_mutex;
	pthread_t heartbeat_thread;
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

	struct caffeine_output *context = bzalloc(sizeof(struct caffeine_output));
	context->output = output;
	pthread_mutex_init_value(&context->stream_mutex);
	pthread_mutex_init(&context->stream_mutex, NULL);

	log_info("caffeine_create");

	context->interface = caff_initialize(caffeine_log, CAFF_LOG_INFO);
	if (!context->interface) {
		log_error("Unable to initialize Caffeine interface");
		bfree(context);
		return NULL;
	}

	return context;
}

static void caffeine_destroy(void *data)
{
	struct caffeine_output *context = data;
	log_info("caffeine_destroy");
	caff_deinitialize(context->interface);
	caffeine_free_stream_info(context->stream_info);

	bfree(data);
}

static char const * caffeine_offer_generated(
	void * data,
	char const * sdp_offer)
{
	struct caffeine_output * context = data;
	log_info("caffeine_offer_generated");

	obs_service_t * service = obs_output_get_service(context->output);
	char const * stage_id =
		obs_service_query(service, CAFFEINE_QUERY_STAGE_ID);
	struct caffeine_auth_info const * auth_info =
		obs_service_query(service, CAFFEINE_QUERY_AUTH_INFO);

	context->stream_info =
		caffeine_start_stream(stage_id, sdp_offer, auth_info);

	return context->stream_info ? context->stream_info->sdp_answer : NULL;
}

static bool caffeine_ice_gathered(
	void *data,
	caff_ice_candidates candidates,
	size_t num_candidates)
{
	struct caffeine_output * context = data;
	log_info("caffeine_ice_gathered");

	obs_service_t * service = obs_output_get_service(context->output);
	struct caffeine_auth_info const * auth_info =
		obs_service_query(service, CAFFEINE_QUERY_AUTH_INFO);

	return caffeine_trickle_candidates(
		candidates, num_candidates, auth_info, context->stream_info);
}

static void caffeine_stream_started(void *data)
{
	struct caffeine_output *context = data;
	log_info("caffeine_stream_started");

	// TODO fix all the concurrency
}

static void caffeine_stream_failed(void *data, caff_error error)
{
	struct caffeine_output *context = data;
	log_error("caffeine_stream_failed: %d", error);

	obs_output_signal_stop(context->output, caffeine_to_obs_error(error));
}

void * golive_thread(void * data)
{
	struct caffeine_output * context = data;
	log_info("caffeine_golive");

	pthread_mutex_lock(&context->stream_mutex);

	obs_service_t * service = obs_output_get_service(context->output);

	char * stage_id = bstrdup(
		obs_service_query(service, CAFFEINE_QUERY_STAGE_ID));
	struct caffeine_auth_info const * auth_info =
		obs_service_query(service, CAFFEINE_QUERY_AUTH_INFO);

	char * stream_id = bstrdup(context->stream_info->stream_id);
	char * signed_payload = bstrdup(context->stream_info->signed_payload);
	pthread_mutex_unlock(&context->stream_mutex);

	char * session_id = NULL;

	if ((session_id = set_stage_live(false, session_id, stage_id, stream_id, auth_info)) == NULL) {
		return NULL;
	}

	if (!create_broadcast(auth_info)) {
		bfree(session_id);
		return NULL;
	}

	set_stage_live(true, session_id, stage_id, stream_id, auth_info);

	pthread_mutex_lock(&context->stream_mutex);
	while (context->stream)
	{
		//set_stage_live(true, stage_id, context->stream_info->stream_id, auth_info);
		send_heartbeat(stream_id, signed_payload, auth_info);
		pthread_mutex_unlock(&context->stream_mutex);
		os_sleep_ms(3000);
		pthread_mutex_lock(&context->stream_mutex);
		set_stage_live(true, session_id, stage_id, stream_id, auth_info);
	}
	pthread_mutex_unlock(&context->stream_mutex);

	bfree(session_id);
	bfree(signed_payload);
	bfree(stream_id);
	bfree(stage_id);
	return NULL;
}

static bool caffeine_start(void *data)
{
	struct caffeine_output *context = data;
	log_info("caffeine_start");

	/* TODO: Most of this work should be on separate thread */

	if (!obs_output_can_begin_data_capture(context->output, 0))
		return false;

	caff_stream_handle stream =
		caff_start_stream(context->interface, context,
			caffeine_offer_generated,
			caffeine_ice_gathered,
			caffeine_stream_started,
			caffeine_stream_failed);

	if (!context)
		return false;

	context->stream = stream;

	obs_output_begin_data_capture(context->output, 0);

	pthread_create(&context->heartbeat_thread, NULL, golive_thread, context);

	return true;
}

static void caffeine_stop(void *data, uint64_t ts)
{
	UNUSED_PARAMETER(ts);

	struct caffeine_output *context = data;
	log_info("caffeine_stop");

	/* TODO: do something with ts? */

	pthread_mutex_lock(&context->stream_mutex);

	caff_end_stream(context->stream);
	caffeine_free_stream_info(context->stream_info);

	context->stream_info = NULL;
	context->stream = NULL;

	pthread_mutex_unlock(&context->stream_mutex);
	pthread_join(context->heartbeat_thread, NULL);


	obs_output_end_data_capture(context->output);
}

static void caffeine_raw_video(void *data, struct video_data *frame)
{
	UNUSED_PARAMETER(frame);

	struct caffeine_output *context = data;

	if (!context->stream)
		return;

	uint32_t width = 1280;
	uint32_t height = 720;
	size_t bytes_per_pixel = 4;
	size_t total_bytes = width * height * bytes_per_pixel;

	caff_send_video(
		context->stream, frame->data[0], total_bytes, width, height);
}

static void caffeine_raw_audio(void *data, struct audio_data *frames)
{
	UNUSED_PARAMETER(frames);

	struct caffeine_output *context = data;

	if (!context->stream)
		return;

	caff_send_audio(context->stream, frames->data[0], frames->frames);
}

struct obs_output_info caffeine_output_info = {
	.id        = "caffeine_output",
	.flags     = OBS_OUTPUT_AV | OBS_OUTPUT_SERVICE,
	.get_name  = caffeine_get_name,
	.create    = caffeine_create,
	.destroy   = caffeine_destroy,
	.start     = caffeine_start,
	.stop      = caffeine_stop,
	.raw_video = caffeine_raw_video,
	.raw_audio = caffeine_raw_audio,
};
