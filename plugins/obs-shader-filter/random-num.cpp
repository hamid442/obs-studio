#include "random-num.h"
#include <random>

std::random_device rd{};
std::mt19937_64 generator(rd());

double rand_double(double min, double max)
{
	std::uniform_real_distribution<double> distribution{min, max};
	return distribution(generator);
}

int rand_int(int min, int max)
{
	std::uniform_int_distribution<int> distribution(min, max);
	return distribution(generator);
}