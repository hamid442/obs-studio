#include "fft.h"
#define M_PI_D   (3.141592653589793238462643383279502884197169399375)

void audio_fft_complex(float *data, int N)
{
	int l = (int)ceil(log2(N));
	/*
	FFTComplex *context = av_fft_init(l, 0);
	av_fft_calc(context, data);
	av_fft_end(context);
	*/
	RDFTContext *context = av_rdft_init(l, DFT_R2C);
	av_rdft_calc(context, data);
	av_rdft_end(context);
}

void audio_ifft_complex(float *data, int N)
{
	int l = (int)ceil(log2(N));
	/*
	FFTComplex *context = av_fft_init(l, 1);
	av_fft_calc(context, data);
	av_fft_end(context);
	*/
	RDFTContext *context = av_rdft_init(l, IDFT_C2R);
	av_rdft_calc(context, data);
	av_rdft_end(context);
}

RDFTContext *av_init_rdft(int bits, enum RDFTransformType transform)
{
	return av_rdft_init(bits, transform);
}

void av_calc_rdft(RDFTContext *context, float *samples)
{
	av_rdft_calc(context, samples);
}

void av_end_rdft(RDFTContext *context)
{
	av_rdft_end(context);
}



/* Must be alphabetically ordered for binary search */
const char *fft_window_strings[] = {
	"bartlett",
	"blackmann",
	"blackmann_exact",
	"blackmann_harris",
	"blackmann_nuttall",
	"flat_top",
	"hann",
	"nuttall",
	"sine",
	"triangular",
	"welch"
};

enum fft_windowing_type get_window_type(const char *window)
{
	enum fft_windowing_type ret;
	if (window) {
		int low_bound = 0;
		int high_bound = end_fft_enum - 1;
		int i;
		int c;
		for (; low_bound <= high_bound;) {
			i = (low_bound + ((high_bound - low_bound) / 2));
			c = strcmp(window, fft_window_strings[i]);
			if (c == 0) {
				return i;
			} else if (c > 0) {
				low_bound = i + 1;
			} else {
				high_bound = i - 1;
			}
		}
		return none;
	} else {
		ret = none;
	}
	return ret;
}

/* from: https://en.wikipedia.org/wiki/Window_function */
void window_function(float *data, int N, enum fft_windowing_type type)
{
	size_t n;
	double d;
	size_t N2;
	double a;
	double a0;
	double a1;
	double a2;
	double a3;
	double a4;
	switch (type) {
	case triangular:
		N2 = N - 1;
		for (n = 0; n < N; n++) {
			d = 1.0 - fabs((n - (N2) / 2.0) / (N / 2.0));
			data[n] *= (float)d;
		}
		break;
	case bartlett:
		N2 = N - 1;
		for (n = 0; n < N; n++) {
			d = 1 - fabs((n - (N2) / 2.0) / (N2 / 2.0));
			data[n] *= (float)d;
		}
		break;
	case welch:
		N2 = N - 1;
		for (n = 0; n < N; n++) {
			d = 1 - pow((n - N2 / 2.0) / (N2 / 2.0), 2);
			data[n] *= (float)d;
		}
	case hann:
		N2 = N - 1;
		a0 = 0.5;
		a1 = 0.5;
		for (n = 0; n < N; n++) {
			d = a0 - a1 * cos((2.0 * M_PI_D * n) / N2);
			data[n] *= (float)d;
		}
		break;
	case blackmann:
		a = 0.16;
		a0 = (1 - a) / 2.0;
		a1 = 0.5;
		a2 = a / 2.0;
		goto cossum2;
	case blackmann_exact:
		a0 = 7938.0 / 18608.0;
		a1 = 9240.0 / 18608.0;
		a2 = 1430.0 / 18608.0;
		goto cossum2;
	case nuttall:
		a0 = 0.355768;
		a1 = 0.487396;
		a2 = 0.144232;
		a3 = 0.012604;
		goto cossum3;
	case blackmann_nuttall:
		a0 = 0.3635819;
		a1 = 0.4891775;
		a2 = 0.1365995;
		a3 = 0.0106411;
		goto cossum3;
	case blackmann_harris:
		a0 = 0.35875;
		a1 = 0.48829;
		a2 = 0.14128;
		a3 = 0.01168;
		goto cossum3;
	case flat_top:
		a0 = 1;
		a1 = 1.93;
		a2 = 1.29;
		a3 = 0.388;
		a4 = 0.028;
		goto cossum4;
	default:
		return;
	}
	return;

cossum2:
	N2 = N - 1;
	for (n = 0; n < N; n++) {
		d = a0 - a1 * cos((2.0 * M_PI_D * n) / N2) +
			a2 * cos((4.0 * M_PI_D * n) / N2);
		data[n] *= (float)d;
	}
	return;
cossum3:
	N2 = N - 1;
	for (n = 0; n < N; n++) {
		d = a0 - a1 * cos((2.0 * M_PI_D * n) / N2) +
			a2 * cos((4.0 * M_PI_D * n) / N2) -
			a3 * cos((6.0 * M_PI_D * n) / N2);
		data[n] *= (float)d;
	}
	return;
cossum4:
	N2 = N - 1;
	for (n = 0; n < N; n++) {
		d = a0 - a1 * cos((2.0 * M_PI_D * n) / N2) +
			a2 * cos((4.0 * M_PI_D * n) / N2) -
			a3 * cos((6.0 * M_PI_D * n) / N2) +
			a4 * cos((8.0 * M_PI_D * n) / N2);
		data[n] *= (float)d;
	}
	return;
}

#define INPUT_SAMPLERATE     48000
#define INPUT_FORMAT         AV_SAMPLE_FMT_FLTP
#define INPUT_CHANNEL_LAYOUT AV_CH_LAYOUT_5POINT0
#define VOLUME_VAL 0.90
static int init_filter_graph(AVFilterGraph **graph, AVFilterContext **src,
		AVFilterContext **sink, int sample_rate, uint64_t layout)
{
	AVFilterGraph *filter_graph;
	AVFilterContext *abuffer_ctx;
	const AVFilter  *abuffer;
	AVFilterContext *volume_ctx;
	const AVFilter  *volume;
	AVFilterContext *aformat_ctx;
	const AVFilter  *aformat;
	AVFilterContext *abuffersink_ctx;
	const AVFilter  *abuffersink;
	AVDictionary *options_dict = NULL;
	uint8_t options_str[1024];
	uint8_t ch_layout[64];
	int err;
	/* Create a new filtergraph, which will contain all the filters. */
	filter_graph = avfilter_graph_alloc();
	if (!filter_graph) {
		fprintf(stderr, "Unable to create filter graph.\n");
		return AVERROR(ENOMEM);
	}
	/* Create the abuffer filter;
	 * it will be used for feeding the data into the graph. */
	abuffer = avfilter_get_by_name("abuffer");
	if (!abuffer) {
		fprintf(stderr, "Could not find the abuffer filter.\n");
		return AVERROR_FILTER_NOT_FOUND;
	}
	abuffer_ctx = avfilter_graph_alloc_filter(filter_graph, abuffer, "src");
	if (!abuffer_ctx) {
		fprintf(stderr, "Could not allocate the abuffer instance.\n");
		return AVERROR(ENOMEM);
	}
	/* Set the filter options through the AVOptions API. */
	av_get_channel_layout_string(ch_layout, sizeof(ch_layout), 0, INPUT_CHANNEL_LAYOUT);
	av_opt_set(abuffer_ctx, "channel_layout", ch_layout, AV_OPT_SEARCH_CHILDREN);
	av_opt_set(abuffer_ctx, "sample_fmt", av_get_sample_fmt_name(INPUT_FORMAT), AV_OPT_SEARCH_CHILDREN);
	av_opt_set_q(abuffer_ctx, "time_base", (AVRational)
	{
		1, sample_rate
	}, AV_OPT_SEARCH_CHILDREN);
	av_opt_set_int(abuffer_ctx, "sample_rate", sample_rate, AV_OPT_SEARCH_CHILDREN);
	/* Now initialize the filter; we pass NULL options, since we have already
	 * set all the options above. */
	err = avfilter_init_str(abuffer_ctx, NULL);
	if (err < 0) {
		fprintf(stderr, "Could not initialize the abuffer filter.\n");
		return err;
	}
	/* Create volume filter. */
	volume = avfilter_get_by_name("volume");
	if (!volume) {
		fprintf(stderr, "Could not find the volume filter.\n");
		return AVERROR_FILTER_NOT_FOUND;
	}
	volume_ctx = avfilter_graph_alloc_filter(filter_graph, volume, "volume");
	if (!volume_ctx) {
		fprintf(stderr, "Could not allocate the volume instance.\n");
		return AVERROR(ENOMEM);
	}
	/* A different way of passing the options is as key/value pairs in a
	 * dictionary. */
	av_dict_set(&options_dict, "volume", AV_STRINGIFY(VOLUME_VAL), 0);
	err = avfilter_init_dict(volume_ctx, &options_dict);
	av_dict_free(&options_dict);
	if (err < 0) {
		fprintf(stderr, "Could not initialize the volume filter.\n");
		return err;
	}
	/* Create the aformat filter;
	 * it ensures that the output is of the format we want. */
	aformat = avfilter_get_by_name("aformat");
	if (!aformat) {
		fprintf(stderr, "Could not find the aformat filter.\n");
		return AVERROR_FILTER_NOT_FOUND;
	}
	aformat_ctx = avfilter_graph_alloc_filter(filter_graph, aformat, "aformat");
	if (!aformat_ctx) {
		fprintf(stderr, "Could not allocate the aformat instance.\n");
		return AVERROR(ENOMEM);
	}
	/* A third way of passing the options is in a string of the form
	 * key1=value1:key2=value2.... */
	snprintf(options_str, sizeof(options_str),
		"sample_fmts=%s:sample_rates=%d:channel_layouts=0x%"PRIx64,
		av_get_sample_fmt_name(AV_SAMPLE_FMT_FLTP), sample_rate,
		layout);
	err = avfilter_init_str(aformat_ctx, options_str);
	if (err < 0) {
		av_log(NULL, AV_LOG_ERROR, "Could not initialize the aformat filter.\n");
		return err;
	}
	/* Finally create the abuffersink filter;
	 * it will be used to get the filtered data out of the graph. */
	abuffersink = avfilter_get_by_name("abuffersink");
	if (!abuffersink) {
		fprintf(stderr, "Could not find the abuffersink filter.\n");
		return AVERROR_FILTER_NOT_FOUND;
	}
	abuffersink_ctx = avfilter_graph_alloc_filter(filter_graph, abuffersink, "sink");
	if (!abuffersink_ctx) {
		fprintf(stderr, "Could not allocate the abuffersink instance.\n");
		return AVERROR(ENOMEM);
	}
	/* This filter takes no options. */
	err = avfilter_init_str(abuffersink_ctx, NULL);
	if (err < 0) {
		fprintf(stderr, "Could not initialize the abuffersink instance.\n");
		return err;
	}
	/* Connect the filters;
	 * in this simple case the filters just form a linear chain. */
	err = avfilter_link(abuffer_ctx, 0, volume_ctx, 0);
	if (err >= 0)
		err = avfilter_link(volume_ctx, 0, aformat_ctx, 0);
	if (err >= 0)
		err = avfilter_link(aformat_ctx, 0, abuffersink_ctx, 0);
	if (err < 0) {
		fprintf(stderr, "Error connecting filters\n");
		return err;
	}
	/* Configure the graph. */
	err = avfilter_graph_config(filter_graph, NULL);
	if (err < 0) {
		av_log(NULL, AV_LOG_ERROR, "Error configuring the filter graph\n");
		return err;
	}
	*graph = filter_graph;
	*src = abuffer_ctx;
	*sink = abuffersink_ctx;
	return 0;
}

/* Construct a frame of audio data to be filtered;
 * this simple example just synthesizes a sine wave. */
static int get_input(AVFrame *frame, int frame_num)
{
	int err, i, j;
#define FRAME_SIZE 1024
	/* Set up the frame properties and allocate the buffer for the data. */
	frame->sample_rate = INPUT_SAMPLERATE;
	frame->format = INPUT_FORMAT;
	frame->channel_layout = INPUT_CHANNEL_LAYOUT;
	frame->nb_samples = FRAME_SIZE;
	frame->pts = frame_num * FRAME_SIZE;
	err = av_frame_get_buffer(frame, 0);
	if (err < 0)
		return err;
	/* Fill the data for each channel. */
	for (i = 0; i < 5; i++) {
		float *data = (float*)frame->extended_data[i];
		for (j = 0; j < frame->nb_samples; j++)
			data[j] = sin(2 * M_PI * (frame_num + j) * (i + 1) / FRAME_SIZE);
	}
	return 0;
}

static int make_frame(AVFrame *frame, int frame_num, int sample_rate,
		uint64_t layout, struct obs_audio_data *data)
{
	int err, i, j;
	frame->sample_rate = sample_rate;
	frame->format = AV_SAMPLE_FMT_FLT;
	frame->channel_layout = layout;
	frame->nb_samples = data->frames;
	frame->pts = data->timestamp;
	err = av_frame_get_buffer(frame, 0);
	if (err < 0)
		return err;
	uint32_t channels = get_audio_channels(layout);
	for (i = 0; i < channels; i++) {

	}
	return 0;
}
