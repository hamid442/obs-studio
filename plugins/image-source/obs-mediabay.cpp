#include <obs-module.h>
#include <util/threading.h>
#include <util/platform.h>
#include <util/darray.h>
#include <util/dstr.h>

#define do_log(level, format, ...) \
	blog(level, "[mediabay: '%s'] " format, \
			obs_source_get_name(ss->source), ##__VA_ARGS__)

#define warn(format, ...)  do_log(LOG_WARNING, format, ##__VA_ARGS__)

