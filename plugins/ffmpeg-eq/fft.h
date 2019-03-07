#pragma once
#include <obs.h>
#include "obs-module.h"

#include <math.h>
#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avfft.h>
#include <libavfilter/avfilter.h>
#include <libavutil/channel_layout.h>
#include <libavutil/md5.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>

/*Should be alphabetically ordered*/
enum fft_windowing_type {
	none = -1,
	rectangular = -1,
	bartlett,
	blackmann,
	blackmann_exact,
	blackmann_harris,
	blackmann_nuttall,
	flat_top,
	hann,
	nuttall,
	sine,
	triangular,
	welch,
	end_fft_enum
};

void audio_fft_complex(float *X, int N);
void audio_ifft_complex(float *X, int N);
void av_calc_rdft(RDFTContext *context, float *samples);
RDFTContext *av_init_rdft(int bits, enum RDFTransformType transform);
void av_end_rdft(RDFTContext *context);
enum fft_windowing_type get_window_type(const char *window);
void window_function(float *data, int N, enum fft_windowing_type type);

#ifdef __cplusplus
}
#endif
