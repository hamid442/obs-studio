#pragma once

#include <obs.h>
#include <math.h>
#include <libavcodec\avfft.h>

enum fft_windowing_type {
	none = 0,
	rectangular = 0,
	triangular,
	bartlett,
	welch,
	sine,
	hann,
	blackmann,
	blackmann_exact,
	nuttall,
	blackmann_nuttall,
	blackmann_harris,
	flat_top
};

void audio_fft_complex(float* X, int N);
enum fft_windowing_type get_window_type(char *window);
void window_function(float *data, int N, enum fft_windowing_type type);