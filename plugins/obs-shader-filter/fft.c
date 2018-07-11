#include "fft.h"
#define M_PI_D   (3.141592653589793238462643383279502884197169399375)

void seperate_fixed(float* a, uint32_t n)
{
	static float tmp[AUDIO_OUTPUT_FRAMES];
	/* copy all odd elements to heap storage */
	for (uint32_t i = 0; i<n / 2; i++)
		tmp[i] = a[i * 2 + 1];
	/* copy all even elements to lower-half of a[] */
	for (uint32_t i = 0; i<n / 2; i++)
		a[i] = a[i * 2];
	/* copy all odd (from heap) to upper-half of a[] */
	for (uint32_t i = 0; i<n / 2; i++)
		a[i + n / 2] = tmp[i];
}

/* N must be a power-of-2, or bad things will happen.
* Currently no check for this condition.
*
* N input samples in X[] are FFT'd and results left in X[].
* Because of Nyquist theorem, N samples means 
* only first N/2 FFT results in X[] are the answer.
* (upper half of X[] is a reflection with no new information).
*/
void _audio_fft_complex(float* X, float* Xi, int N) {
	if (N < 2) {
		/* bottom of recursion. */
		/* Do nothing here, because X[0] = x[0] */
	}
	else {
		seperate_fixed(X, N);
		seperate_fixed(Xi, N);
		//half of N
		int hN = N / 2;

		double w_r = 1.0;
		double w_i = 0.0;
		double q_exp = 0.0;
		_audio_fft_complex(X, Xi, hN);
		_audio_fft_complex(X + hN, Xi + hN, hN);

		for (int k = 0; k < hN; k++) {
			double e_r = X[k];
			double e_i = Xi[k];

			double o_r = X[k + hN];
			double o_i = Xi[k + hN];

			double t = -2.0*M_PI_D*k / N;
			w_r = cos(t);
			w_i = sin(t);
			/* for audio inputs the imaginary component is 0 */
			/* w * o; */
			double wo_r = (w_r * o_r) - (w_i * o_i);
			double wo_i = (w_r * o_i) + (w_i * o_r);

			X[k] = e_r + wo_r; 
			X[k + hN] = e_r - wo_r;

			Xi[k] = e_i + wo_i;
			Xi[k + hN] = e_i - wo_i;
		}
	}
}

/* stuffs complex results in upper half */
void audio_fft_complex(float* X, int N) {
	float Xi[AUDIO_OUTPUT_FRAMES] = { 0 };
	_audio_fft_complex(X, Xi, N);
	int hN = N / 2;
	/* copy the complex components */
	memcpy(&(X[hN]), &(Xi[0]), hN*sizeof(float));
}