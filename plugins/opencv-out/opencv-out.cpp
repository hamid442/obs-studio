#include <obs-module.h>
#include <util/threading.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <opencv2/opencv.hpp>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("opencv_out", "en-US")
#define blog(level, msg, ...) blog(level, "opencv-out: " msg, ##__VA_ARGS__)

#define _MT obs_module_text

#ifdef HAVE_CUDA

#endif

struct opencv_frame_data {
	uint8_t *data;
	cv::Mat frame;
	std::vector<cv::Rect> detected_regions;
	uint64_t timestamp;
	gs_texture_t *tex;
};

struct opencv_filter_data {
	obs_source_t *context;
	obs_data_t *settings;
	
	gs_texrender_t *texrender;
	gs_stagesurf_t *surf;

	gs_texture_t *tex;

	gs_effect_t *effect;
	gs_eparam_t *image;

	pthread_t opencv_thread;
	pthread_cond_t frame_sent;
	pthread_mutex_t opencv_thread_mutex;

	bool thread_created;
	bool run_thread;
	
	size_t size;
	size_t linesize;
	
	int total_width;
	int total_height;

	bool read_texture;
	bool update_classifier;
	bool valid_classifier;

	std::string classifier_path;
	cv::CascadeClassifier cascade;

	std::list<opencv_frame_data> *frames;
	std::list<opencv_frame_data> *processed_frames;

	uint64_t delay;
};

static const char *opencv_filter_get_name(void *unused){
	UNUSED_PARAMETER(unused);
	return _MT("OpencvFilter");
}

void opencv_frame_data_release(struct opencv_frame_data &ofd)
{
	obs_enter_graphics();
	if (ofd.tex) {
		gs_texture_destroy(ofd.tex);
		ofd.tex = NULL;
	}
	if (ofd.data) {
		bfree(ofd.data);
		ofd.data = NULL;
	}
	obs_leave_graphics();
}

void *opencv_thread(void *data)
{
	struct opencv_filter_data *filter = (struct opencv_filter_data*)data;

	std::string n = "opencv_thread";
	os_set_thread_name(n.c_str());
	filter->run_thread = true;
	do {
		int rc = 0;
		pthread_mutex_lock(&filter->opencv_thread_mutex);
		while (filter->run_thread && !filter->frames->empty()) {
			struct timespec stop;
			uint64_t ts = os_gettime_ns();
			stop.tv_nsec = ts + 500000000;
			stop.tv_sec = (ts / 1000000000) + 1;
			rc = pthread_cond_timedwait(&filter->frame_sent,
					&filter->opencv_thread_mutex, &stop);
			if (rc != ETIMEDOUT)
				break;
		}
		pthread_mutex_unlock(&filter->opencv_thread_mutex);

		while (filter->run_thread && !filter->frames->empty()) {
			opencv_frame_data ofd = filter->frames->front();
			filter->frames->pop_front();
			if (filter->valid_classifier) {
				uint64_t ts = os_gettime_ns();
				cv::Mat frame_gray;
				cv::cvtColor(ofd.frame, frame_gray,
					cv::COLOR_RGBA2GRAY);
				cv::equalizeHist(frame_gray, frame_gray);
				filter->cascade.detectMultiScale(frame_gray,
					ofd.detected_regions, 1.3, 5);
				uint64_t sts = os_gettime_ns();
				ts = sts - ts;
				if (ts > filter->delay)
					filter->delay = ts;
				size_t sz = ofd.detected_regions.size();
				for (size_t i = 0;
						i < ofd.detected_regions.size();
						i++) {
					blog(LOG_INFO, "ROI: [<%i,%i>, <%i,%i>] %f ms", ofd.detected_regions[i].x, ofd.detected_regions[i].y,
						ofd.detected_regions[i].width, ofd.detected_regions[i].height, ts * 0.000001);
					/*
					cv::rectangle(ofd.frame,
							ofd.detected_regions[i],
							cv::Scalar(0, 255, 0),
							10);
							*/
				}

				opencv_frame_data_release(ofd);
				/* (const uint8_t**)ofd.frame.ptr() */
				/*
				const uint8_t *p = ofd.frame.data;
				obs_enter_graphics();
				ofd.tex = gs_texture_create(ofd.frame.cols,
						ofd.frame.rows, GS_RGBA, 1,
						(const uint8_t**)&p, 0);
				obs_leave_graphics();
				*/
			}


			/*
			filter->processed_frames->push_back(ofd);
			blog(LOG_INFO, "Frames: [%i, %i]",
					filter->processed_frames->size(),
					filter->frames->size());
					*/
		}

	} while (filter->run_thread);

	return NULL;
}

static void *opencv_filter_create(obs_data_t *settings, obs_source_t *source)
{
	struct opencv_filter_data *filter = (struct opencv_filter_data *)
			bzalloc(sizeof(struct opencv_filter_data));
	filter->context = source;
	filter->settings = settings;
	filter->classifier_path = "";
	filter->valid_classifier = false;
	filter->frames = new std::list<opencv_frame_data>();
	filter->processed_frames = new std::list<opencv_frame_data>();
	
	filter->texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	filter->effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	filter->image = gs_effect_get_param_by_name(filter->effect,
			"image");

	pthread_mutexattr_t attr;

	if (pthread_mutexattr_init(&attr) != 0)
		goto fail;
	if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0)
		goto fail;
	if (pthread_mutex_init(&filter->opencv_thread_mutex, &attr) != 0)
		goto fail;

	pthread_condattr_t cond_attr;
	pthread_condattr_init(&cond_attr);

	if (pthread_cond_init(&filter->frame_sent, &cond_attr))
		goto fail;

	if (pthread_create(&filter->opencv_thread, NULL,
			&opencv_thread, filter))
		filter->thread_created = false;
	else
		filter->thread_created = true;
	
	obs_source_update(source, settings);
	
	return filter;
fail:
	return NULL;
}

static void opencv_filter_destroy(void *data)
{
	struct opencv_filter_data *filter = (struct opencv_filter_data *)data;
	obs_data_release(filter->settings);

	filter->run_thread = false;
	pthread_join(filter->opencv_thread, NULL);
	
	obs_enter_graphics();
	gs_texrender_destroy(filter->texrender);
	gs_stagesurface_destroy(filter->surf);

	while(!filter->frames->empty()) {
		opencv_frame_data ofd = filter->frames->front();
		filter->frames->pop_front();
		gs_texture_destroy(ofd.tex);
		if(ofd.data)
			bfree(ofd.data);
	}

	while(!filter->processed_frames->empty()) {
		opencv_frame_data ofd = filter->processed_frames->front();
		filter->processed_frames->pop_front();
		gs_texture_destroy(ofd.tex);
		if(ofd.data)
			bfree(ofd.data);
	}

	delete filter->frames;
	delete filter->processed_frames;

	pthread_mutex_destroy(&filter->opencv_thread_mutex);
	pthread_cond_destroy(&filter->frame_sent);

	obs_leave_graphics();
	
	bfree(filter);
}

static obs_properties_t *opencv_filter_properties(void *data)
{
	struct opencv_filter_data *filter = (struct opencv_filter_data *)data;
	obs_properties_t *props = obs_properties_create();
	
	obs_properties_add_path(props, "classifier_path",
			_MT("OpenCV.Classifier"), OBS_PATH_FILE, NULL, NULL);
	
	return props;
}

static void opencv_filter_update(void *data, obs_data_t *settings)
{
	struct opencv_filter_data *filter = (struct opencv_filter_data *)data;
	const char* classifier_path = obs_data_get_string(settings,
			"classifier_path");
	std::string c_path(classifier_path);
	if (filter->classifier_path != c_path)
		filter->update_classifier = true;
	filter->classifier_path = c_path;
}

static void opencv_filter_tick(void *data, float seconds)
{
	struct opencv_filter_data *filter = (struct opencv_filter_data*)data;
	obs_source_t *target = obs_filter_get_target(filter->context);
	
	int base_width = obs_source_get_base_width(target);
	int base_height = obs_source_get_base_height(target);
	
	int surf_cx = 0;
	int surf_cy = 0;
	int src_cx = obs_source_get_width(target);
	int src_cy = obs_source_get_height(target);

	if (filter->update_classifier) {
		filter->valid_classifier = filter->cascade.load(filter->classifier_path);
		filter->update_classifier = false;
	}

	if(filter->surf){
		obs_enter_graphics();
		surf_cx = gs_stagesurface_get_width(filter->surf);
		surf_cy = gs_stagesurface_get_height(filter->surf);
		if (surf_cx != src_cx || surf_cy != src_cy) {
			gs_stagesurface_destroy(filter->surf);
			filter->surf = NULL;
		}
		obs_leave_graphics();
	}
	
	size_t linesize = base_width * 4;
	size_t size = linesize * base_height;
	
	filter->total_width = base_width;
	filter->total_height = base_height;
	
	if (!filter->surf) {
		obs_enter_graphics();
		filter->surf = gs_stagesurface_create(base_width, base_height, GS_RGBA);
		obs_leave_graphics();
	}
	
	filter->size = size;
	filter->linesize = linesize;
}

static gs_texture_t *render_original(void *data, gs_effect_t *effect,
		float source_cx, float source_cy)
{
	UNUSED_PARAMETER(effect);

	struct opencv_filter_data *filter = (struct opencv_filter_data *)data;
	obs_source_t *target = obs_filter_get_target(filter->context);
	obs_source_t *parent = obs_filter_get_parent(filter->context);

	if (!target || !parent)
		return NULL;

	gs_texrender_reset(filter->texrender);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	if (gs_texrender_begin(filter->texrender, source_cx, source_cy)) {
		uint32_t parent_flags = obs_source_get_output_flags(target);
		bool custom_draw = (parent_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
		bool async = (parent_flags & OBS_SOURCE_ASYNC) != 0;
		struct vec4 clear_color;

		vec4_zero(&clear_color);
		gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
		gs_ortho(0.0f, source_cx, 0.0f, source_cy,
			-100.0f, 100.0f);

		if (target == parent && !custom_draw && !async)
			obs_source_default_render(target);
		else
			obs_source_video_render(target);

		gs_texrender_end(filter->texrender);
	}

	gs_blend_state_pop();
	
	filter->read_texture = true;
	return gs_texrender_get_texture(filter->texrender);
}

/*
void detectAndDisplay(struct opencv_filter_data *filter)
{
	if (!filter->valid_classifier || filter->frame.empty())
		return;
	cv::Mat frame_gray;
	cv::cvtColor(filter->frame, frame_gray, cv::COLOR_RGBA2GRAY);
	cv::equalizeHist(frame_gray, frame_gray);
	
	filter->detected_regions.clear();
	filter->cascade.detectMultiScale(frame_gray, filter->detected_regions);
	
	for (size_t i = 0; i < filter->detected_regions.size(); i++) {
		blog(LOG_INFO, "ROI: [<%i,%i>, <%i,%i>]", filter->detected_regions[i].x, filter->detected_regions[i].y,
			filter->detected_regions[i].width, filter->detected_regions[i].height);
		//cv::rectangle(filter->frame, filter->detected_regions[i], cv::Scalar(255, 0, 0), 2);
	}
	
	//cv::imshow("Detection", filter->frame);
}
*/
/*
//face_cascade.detectMultiScale(frame_gray, faces);
for (size_t i = 0; i < faces.size(); i++) {
	Point center(faces[i].x + faces[i].width / 2, faces[i].y + faces[i].height / 2);
	ellipse(frame, center, Size(faces[i].width / 2, faces[i].height / 2), 0, 0, 360, Scalar(255, 0, 255), 4);
	Mat faceROI = frame_gray(faces[i]);
	//-- In each face, detect eyes
	std::vector<Rect> eyes;
	eyes_cascade.detectMultiScale(faceROI, eyes);
	for (size_t j = 0; j < eyes.size(); j++) {
		Point eye_center(faces[i].x + eyes[j].x + eyes[j].width / 2, faces[i].y + eyes[j].y + eyes[j].height / 2);
		int radius = cvRound((eyes[j].width + eyes[j].height)*0.25);
		circle(frame, eye_center, radius, Scalar(255, 0, 0), 4);
	}
}
*/
static void process_surf(void *data, size_t source_cx, size_t source_cy)
{
	struct opencv_filter_data *filter = (struct opencv_filter_data *)data;
	obs_source_t *parent = obs_filter_get_parent(filter->context);
	if (!parent || !filter->context)
		return;
	/*
	const char* p = obs_source_get_name(parent);
	const char* f = obs_source_get_name(filter->context);
	*/
	uint8_t *tex_data = NULL;
	if (filter->read_texture && filter->surf) {
		filter->read_texture = false;
		if (filter->frames->size() > 0)
			return;
		if (gs_stagesurface_map(filter->surf, &tex_data,
				(uint32_t*)&filter->linesize)) {
			/* write to buffer */
			/* os_unlink(path.array); */
			/* memcpy(filter->data, tex_data, filter->size); */
			if (filter->thread_created) {
				struct opencv_frame_data f = { 0 };
				f.data = (uint8_t*)bmemdup(tex_data,
						filter->size);
				f.frame = cv::Mat(cvSize(source_cx, source_cy),
						CV_8UC4, f.data);
				f.tex = NULL;
				if (!f.frame.isContinuous()) {
					blog(LOG_WARNING, "There will be problems");
					bfree(f.data);
				} else {
					filter->frames->push_back(f);
					pthread_cond_broadcast(&filter->frame_sent);
				}
			}
			/*
			filter->frame = cv::Mat(cvSize(source_cx, source_cy),
					CV_8UC4, tex_data);
					*/
			gs_stagesurface_unmap(filter->surf);
			/*
			detectAndDisplay(filter);
			*/
			/*
			os_quick_write_utf8_file(filter->path.array,
					(char*)filter->data, filter->size,
					false);
					*/

		}
	}
}

static void opencv_filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct opencv_filter_data *filter = (struct opencv_filter_data *)data;
	
	float src_cx = (float)obs_source_get_width(filter->context);
	float src_cy = (float)obs_source_get_height(filter->context);
	
	gs_texture_t *tex = render_original(data, NULL, src_cx, src_cy);
	if(filter->surf)
		gs_stage_texture(filter->surf, tex);
	filter->read_texture = true;

	if (filter->processed_frames->empty() || filter->effect == NULL) {
		obs_source_skip_video_filter(filter->context);
	} else {
		opencv_frame_data ofd = filter->processed_frames->front();
		filter->processed_frames->pop_front();
		/*
		obs_enter_graphics();
		if (filter->tex)
			gs_texture_destroy(filter->tex);
		filter->tex = gs_texture_create(ofd.frame.cols, ofd.frame.rows,
			GS_RGBA, 1, (const uint8_t**)ofd.frame.ptr(), 0);
		obs_leave_graphics();
		*/
		filter->tex = ofd.tex;
		//bfree(ofd.data);
		if (!obs_source_process_filter_begin(filter->context, GS_RGBA,
				OBS_NO_DIRECT_RENDERING)) {
			obs_enter_graphics();
			gs_texture_destroy(ofd.tex);
			obs_leave_graphics();
			bfree(ofd.data);
			return;
		}
		gs_effect_set_texture(filter->image, filter->tex);
		obs_source_process_filter_end(filter->context, filter->effect,
			ofd.frame.cols, ofd.frame.rows);

		obs_enter_graphics();
		gs_texture_destroy(ofd.tex);
		obs_leave_graphics();
		bfree(ofd.data);
		//bfree(ofd.data);
		/*
		for (size_t i = 0; i < ofd.detected_regions.size(); i++) {
			
		}
		*/

	}
	
	process_surf(filter, src_cx, src_cy);
}

static uint32_t opencv_filter_width(void *data)
{
	struct opencv_filter_data *filter = (struct opencv_filter_data *)data;
	return filter->total_width;
}

static uint32_t opencv_filter_height(void *data)
{
	struct opencv_filter_data *filter = (struct opencv_filter_data *)data;
	return filter->total_height;
}

static void opencv_filter_defaults(obs_data_t *settings)
{
}
		
bool obs_module_load(void)
{
	struct obs_source_info opencv_filter = { 0 };
	opencv_filter.id = "opencv_out";
	opencv_filter.type = OBS_SOURCE_TYPE_FILTER;
	opencv_filter.output_flags = OBS_SOURCE_VIDEO;
	opencv_filter.create = opencv_filter_create;
	opencv_filter.destroy = opencv_filter_destroy;
	opencv_filter.update = opencv_filter_update;
	opencv_filter.video_tick = opencv_filter_tick;
	opencv_filter.get_name = opencv_filter_get_name;
	opencv_filter.get_defaults = opencv_filter_defaults;
	opencv_filter.get_width = opencv_filter_width;
	opencv_filter.get_height = opencv_filter_height;
	opencv_filter.video_render = opencv_filter_render;
	opencv_filter.get_properties = opencv_filter_properties;

	obs_register_source(&opencv_filter);

	blog(LOG_INFO, "%s", cv::getBuildInformation().c_str());
	int deviceCount = cv::cuda::getCudaEnabledDeviceCount();
	switch (deviceCount) {
	case -1:
		blog(LOG_INFO, "CUDA not installed or incompatible");
		break;
	case 0:
		blog(LOG_INFO, "Compiled w/o CUDA");
		break;
	default:
		blog(LOG_INFO, "Devices w/ CUDA enabled: %i", deviceCount);
		break;
	}
	return true;
}

void obs_module_unload(void)
{
}
