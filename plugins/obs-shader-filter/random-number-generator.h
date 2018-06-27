#ifdef __cplusplus
extern "C" {
#endif

double random_number(double min, double max);
int32_t random_number_int(int32_t min, int32_t max);
int64_t random_number_int64(int64_t min, int64_t max);
double random_number_jitter(double min, double max, double jitter, double strength);

#ifdef __cplusplus
} // closing brace for extern "C"
#endif
