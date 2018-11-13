#include <obs-module.h>

#include <util/base.h>
#include <util/platform.h>
#include <util/threading.h>

#include <caffeine.h>

#include "caffeine-api.h"
#include "caffeine-service.h"

#define CAFFEINE_LOG_TITLE "caffeine output"
#include "caffeine-log.h"

/* Uncomment this to log each call to raw_audio/video
#define TRACE_FRAMES
/**/

enum state {
	OFFLINE = 0,
	STARTING,
	ONLINE,
	STOPPING,
};

struct caffeine_output
{
	obs_output_t * output;
	caff_interface_handle interface;
	caff_stream_handle stream;
	struct caffeine_stream_info * stream_info;
	pthread_mutex_t stream_mutex;
	pthread_t heartbeat_thread;
	struct obs_video_info video_info;

	volatile long state;
};

static const char *caffeine_get_name(void *data)
{
	UNUSED_PARAMETER(data);

	return obs_module_text("CaffeineOutput");
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
	case CAFF_ERROR_SDP_OFFER:
	case CAFF_ERROR_SDP_ANSWER:
	case CAFF_ERROR_ICE_TRICKLE:
		return OBS_OUTPUT_CONNECT_FAILED;
	case CAFF_ERROR_DISCONNECTED:
		return OBS_OUTPUT_DISCONNECTED;
	default:
		return OBS_OUTPUT_ERROR;
	}
}

caff_format obs_to_caffeine_format(enum video_format format)
{
	switch (format)
	{
	case VIDEO_FORMAT_I420:
		return CAFF_FORMAT_I420;
	case VIDEO_FORMAT_NV12:
		return CAFF_FORMAT_NV12;
	case VIDEO_FORMAT_YUY2:
		return CAFF_FORMAT_YUY2;
	case VIDEO_FORMAT_UYVY:
		return CAFF_FORMAT_UYVY;
	case VIDEO_FORMAT_BGRA:
		return CAFF_FORMAT_BGRA;

	case VIDEO_FORMAT_RGBA:
	case VIDEO_FORMAT_I444:
	case VIDEO_FORMAT_Y800:
	case VIDEO_FORMAT_BGRX:
	case VIDEO_FORMAT_YVYU:
	default:
		return CAFF_FORMAT_UNKNOWN;
	}
}

/* TODO: figure out why caffeine-rtc isn't calling this */
static void caffeine_log(caff_log_severity severity, char const * message)
{
	int log_level = caffeine_to_obs_log_level(severity);
	if (log_level)
		blog(log_level, "[caffeine-rtc] %s", message);
}

static void *caffeine_create(obs_data_t *settings, obs_output_t *output)
{
	trace();
	UNUSED_PARAMETER(settings);

	struct caffeine_output *context = bzalloc(sizeof(struct caffeine_output));
	context->output = output;

	pthread_mutex_init(&context->stream_mutex, NULL);

	context->interface = caff_initialize(caffeine_log, CAFF_LOG_INFO);
	if (!context->interface) {
		log_error("Unable to initialize Caffeine interface");
		bfree(context);
		return NULL;
	}

	return context;
}

static inline enum state get_state(struct caffeine_output * context)
{
	return (enum state)os_atomic_load_long(&context->state);
}

static inline bool require_state(
	struct caffeine_output * context,
	enum state expected_state)
{
	enum state state = get_state(context);
	if (state != expected_state) {
		log_error("In state %d when expecting %d",
			state, expected_state);
		return false;
	}
	return true;
}

static inline void set_state(struct caffeine_output * context, enum state state)
{
	os_atomic_set_long(&context->state, (long)state);
}

static inline bool transition_state(
	struct caffeine_output * context,
	enum state old_state,
	enum state new_state)
{
	bool result = os_atomic_compare_swap_long(&context->state, (long)old_state,
					(long)new_state);
	if (!result)
		log_error("Transitioning to state %d expects state %d",
			new_state, old_state);
	return result;
}

static char const * caffeine_offer_generated(void * data, char const * sdp_offer);
static bool caffeine_ice_gathered(void *data, caff_ice_candidates candidates, size_t num_candidates);
static void caffeine_stream_started(void *data);
static void caffeine_stream_failed(void *data, caff_error error);

static int const enforced_height = 720;
static double const max_ratio = 3.0;
static double const min_ratio = 1.0/3.0;

static bool caffeine_start(void *data)
{
	trace();
	struct caffeine_output *context = data;

	if (!obs_get_video_info(&context->video_info)) {
		log_error("Failed to get video info");
		return false;
	}

	/* TODO: enforce this in UI and/or rescale in RTC layer */
	if (context->video_info.output_height != enforced_height) {
		log_error("Video output must be 720 pixels high for Caffeine");
		return false;
	}

	double ratio = (double)context->video_info.output_width /
		context->video_info.output_height;
	if (ratio < min_ratio || ratio > max_ratio) {
		log_error("Aspect ratio of output must be >= 1:3 or <= 3:1 for "
			  "Caffeine");
		return false;
	}

	caff_format format =
		obs_to_caffeine_format(context->video_info.output_format);

	if (format == CAFF_FORMAT_UNKNOWN) {
		log_error("Unsupported video format %s",
			get_video_format_name(context->video_info.output_format));
		return false;
	}

	struct audio_convert_info conversion = {
		.format = AUDIO_FORMAT_16BIT,
		.speakers = SPEAKERS_STEREO,
		.samples_per_sec = 48000
	};
	obs_output_set_audio_conversion(context->output, &conversion);

	if (!obs_output_can_begin_data_capture(context->output, 0))
		return false;

	if (!transition_state(context, OFFLINE, STARTING))
		return false;

	caff_stream_handle stream =
		caff_start_stream(context->interface, context,
			caffeine_offer_generated, caffeine_ice_gathered,
			caffeine_stream_started, caffeine_stream_failed);
	if (!stream) {
		set_state(context, OFFLINE);
		log_error("Failed to start stream");
		return false;
	}

	pthread_mutex_lock(&context->stream_mutex);
	context->stream = stream;
	pthread_mutex_unlock(&context->stream_mutex);

	return true;
}

/* Called from another thread, blocking OK */
static char const * caffeine_offer_generated(
	void * data,
	char const * sdp_offer)
{
	trace();
	struct caffeine_output * context = data;

	if (!require_state(context, STARTING))
		return NULL;

	obs_service_t * service = obs_output_get_service(context->output);
	char const * stage_id =
		obs_service_query(service, CAFFEINE_QUERY_STAGE_ID);
	struct caffeine_credentials * creds =
		obs_service_query(service, CAFFEINE_QUERY_CREDENTIALS);

	struct caffeine_stream_info * stream_info =
		caffeine_start_stream(stage_id, sdp_offer, creds);

	pthread_mutex_lock(&context->stream_mutex);
	context->stream_info = stream_info;
	pthread_mutex_unlock(&context->stream_mutex);

	return stream_info ? stream_info->sdp_answer : NULL;
}

/* Called from another thread, blocking OK */
static bool caffeine_ice_gathered(
	void *data,
	caff_ice_candidates candidates,
	size_t num_candidates)
{
	trace();
	struct caffeine_output * context = data;

	obs_service_t * service = obs_output_get_service(context->output);
	struct caffeine_credentials * creds =
		obs_service_query(service, CAFFEINE_QUERY_CREDENTIALS);

	pthread_mutex_lock(&context->stream_mutex);
	struct caffeine_stream_info info = {
		.stream_id = bstrdup(context->stream_info->stream_id),
		.sdp_answer = bstrdup(context->stream_info->sdp_answer),
		.signed_payload = bstrdup(context->stream_info->signed_payload),
	};
	pthread_mutex_unlock(&context->stream_mutex);

	bool result = caffeine_trickle_candidates(
		candidates, num_candidates, creds, context->stream_info);

	bfree(info.stream_id);
	bfree(info.sdp_answer);
	bfree(info.signed_payload);

	return result;
}

static void * heartbeat(void * data);

static void caffeine_stream_started(void *data)
{
	trace();
	struct caffeine_output *context = data;

	if (!transition_state(context, STARTING, ONLINE)) {
		return;
	}

	obs_output_begin_data_capture(context->output, 0);

	pthread_create(&context->heartbeat_thread, NULL, heartbeat, context);
}

static void caffeine_stop_stream(struct caffeine_output * context);

static void caffeine_stream_failed(void *data, caff_error error)
{
	log_error("Stream failed: [%d] %s", error, caff_error_string(error));

	struct caffeine_output *context = data;

	set_state(context, STOPPING);
	caffeine_stop_stream(context);

	obs_output_signal_stop(context->output, caffeine_to_obs_error(error));
}

static void * heartbeat(void * data)
{
	trace();
	os_set_thread_name("Caffeine heartbeat");

	struct caffeine_output * context = data;

	pthread_mutex_lock(&context->stream_mutex);
	if (!require_state(context, ONLINE)) {
		pthread_mutex_unlock(&context->stream_mutex);
		return NULL;
	}
	char * stream_id = bstrdup(context->stream_info->stream_id);
	char * signed_payload = bstrdup(context->stream_info->signed_payload);
	pthread_mutex_unlock(&context->stream_mutex);

	obs_service_t * service = obs_output_get_service(context->output);
	char * stage_id = bstrdup(
		obs_service_query(service, CAFFEINE_QUERY_STAGE_ID));
	char * title = bstrdup(
		obs_service_query(service, CAFFEINE_QUERY_BROADCAST_TITLE));
	struct caffeine_credentials * creds =
		obs_service_query(service, CAFFEINE_QUERY_CREDENTIALS);
	enum caffeine_rating rating = (enum caffeine_rating)
		obs_service_query(service, CAFFEINE_QUERY_BROADCAST_RATING);

	char * session_id = set_stage_live(false, NULL, stage_id,
		stream_id, title, rating, creds);
	if (!session_id) {
		caffeine_stream_failed(data, CAFF_ERROR_UNKNOWN);
		goto get_session_error;
	}

	if (!create_broadcast(title, rating, creds)) {
		goto create_broadcast_error;
	}

	/* TODO: use wall time instead of accumulation of sleep time */
	long const heartbeat_interval = 5000; /* ms */
	long const check_interval = 100;

	long interval = heartbeat_interval;

	static int const max_failures = 5;
	int failures = 0;

	for (enum state state = get_state(context);
		state == ONLINE;
		os_sleep_ms(check_interval), state = get_state(context))
	{
		interval += check_interval;
		if (interval < heartbeat_interval)
			continue;

		interval = 0;

		set_stage_live(true, session_id, stage_id, stream_id, title,
				rating, creds);

		if (send_heartbeat(stream_id, signed_payload, creds)) {
			failures = 0;
			continue;
		}
		log_debug("Heartbeat failed");

		++failures;
		if (failures > max_failures) {
			log_error("Heartbeat failed %d times; ending stream.",
				failures);
			caffeine_stream_failed(data, CAFF_ERROR_DISCONNECTED);
		}
	}

	set_stage_live(false, session_id, stage_id, stream_id, title, rating,
			creds);

create_broadcast_error:
	bfree(session_id);
get_session_error:
	bfree(title);
	bfree(stage_id);
	bfree(signed_payload);
	bfree(stream_id);
	return NULL;
}

static void caffeine_raw_video(void *data, struct video_data *frame)
{
#ifdef TRACE_FRAMES
	trace();
#endif
	struct caffeine_output *context = data;

	uint32_t width = context->video_info.output_width;
	uint32_t height = context->video_info.output_height;
	size_t total_bytes = frame->linesize[0] * height;
	caff_format format =
		obs_to_caffeine_format(context->video_info.output_format);

	pthread_mutex_lock(&context->stream_mutex);
	if (context->stream)
		caff_send_video(context->stream, frame->data[0], total_bytes,
			width, height, format);
	pthread_mutex_unlock(&context->stream_mutex);
}

static void caffeine_raw_audio(void *data, struct audio_data *frames)
{
#ifdef TRACE_FRAMES
	trace();
#endif
	struct caffeine_output *context = data;

	pthread_mutex_lock(&context->stream_mutex);
	if (context->stream)
		caff_send_audio(context->stream, frames->data[0],
			frames->frames);
	pthread_mutex_unlock(&context->stream_mutex);
}

static void caffeine_stop_stream(struct caffeine_output * context)
{
	trace();
	pthread_mutex_lock(&context->stream_mutex);

	if (context->stream)
		caff_end_stream(context->stream);

	if (context->stream_info)
		caffeine_free_stream_info(context->stream_info);

	context->stream_info = NULL;
	context->stream = NULL;

	pthread_mutex_unlock(&context->stream_mutex);

	set_state(context, OFFLINE);
}

static void caffeine_stop(void *data, uint64_t ts)
{
	trace();
	/* TODO: do something with this? */
	UNUSED_PARAMETER(ts);

	struct caffeine_output *context = data;

	obs_output_end_data_capture(context->output);

	set_state(context, STOPPING);
	pthread_join(context->heartbeat_thread, NULL);

	caffeine_stop_stream(context);
}

static void caffeine_destroy(void *data)
{
	trace();
	struct caffeine_output *context = data;
	caff_deinitialize(context->interface);
	pthread_mutex_destroy(&context->stream_mutex);

	bfree(data);
}

struct obs_output_info caffeine_output_info = {
	.id        = "caffeine_output",
	.flags     = OBS_OUTPUT_AV | OBS_OUTPUT_SERVICE,
	.get_name  = caffeine_get_name,
	.create    = caffeine_create,
	.start     = caffeine_start,
	.raw_video = caffeine_raw_video,
	.raw_audio = caffeine_raw_audio,
	.stop      = caffeine_stop,
	.destroy   = caffeine_destroy,
};
