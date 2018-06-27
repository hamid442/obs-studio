#include <obs.h>
#include <util/platform.h>
#include <random>
#include "SimplexNoise.h"
#include <cstdint>

static std::random_device rd;
static std::mt19937 mt_gen(rd());
static std::mt19937_64 mt_64_gen(rd());

double random_number(double min, double max) {
	std::uniform_real_distribution<double> distribution(min, max);
	return distribution(mt_64_gen);
}

int32_t random_number_int(int32_t min, int32_t max) {
	std::uniform_int_distribution<int32_t> distribution(min, max);
	return distribution(mt_gen);
}

int64_t random_number_int64(int64_t min, int64_t max) {
	std::uniform_int_distribution<int64_t> distribution(min, max);
	return distribution(mt_gen);
}

#define NS_TO_SEC 0.000000001
double random_number_jitter(double min, double max, double jitter, double strength) {
	double ret;
	float ts = (float)os_gettime_ns() * NS_TO_SEC;
	static SimplexNoise sn = SimplexNoise();
	bool jit = random_number(0, 1.0) <= jitter;
	if(jit) {
		ret = sn.noise(min, max, ts - (strength / 2.0) + (random_number(0, strength)));
	} else {
		ret = sn.noise(min, max, ts);
	}
	return ret;
}
