project(obs-filters)

option(DISABLE_SPEEXDSP "Disable building of the SpeexDSP-based Noise Suppression filter" OFF)

find_package(Qt5Core REQUIRED)
find_package(Qt5Widgets REQUIRED)

set(CMAKE_AUTOMOC TRUE)

if(DISABLE_SPEEXDSP)
	message(STATUS "SpeexDSP support disabled")
	set(LIBSPEEXDSP_FOUND FALSE)
else()
	find_package(Libspeexdsp QUIET)

	if(NOT LIBSPEEXDSP_FOUND)
		message(STATUS "SpeexDSP support not found")
		set(LIBSPEEXDSP_FOUND FALSE)
	else()
		message(STATUS "SpeexDSP supported")
		set(obs-filters_LIBSPEEXDSP_SOURCES
			noise-suppress-filter.c)
		set(obs-filters_LIBSPEEXDSP_LIBRARIES
			${LIBSPEEXDSP_LIBRARIES})
	endif()
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/obs-filters-config.h.in"
	"${CMAKE_BINARY_DIR}/plugins/obs-filters/config/obs-filters-config.h")

set(obs-filters_config_HEADERS
	"${CMAKE_BINARY_DIR}/plugins/obs-filters/config/obs-filters-config.h")
include_directories(${LIBSPEEXDSP_INCLUDE_DIRS}
#	"${CMAKE_BINARY_DIR}/libobs"
	"${CMAKE_BINARY_DIR}/plugins/obs-filters/config"
	"${CMAKE_BINARY_DIR}/UI")
#	"${CMAKE_BINARY_DIR}/UI/obs-frontend-api")
#include_directories(SYSTEM "obs-frontend-api")

if(MSVC)
	set(obs-filters_PLATFORM_DEPS
		w32-pthreads)
endif()

set(obs-filters_SOURCES
	obs-filters.c
	color-correction-filter.c
	async-delay-filter.c
	gpu-delay.c
	crop-filter.c
	scale-filter.c
	scroll-filter.c
	chroma-key-filter.c
	color-key-filter.c
	color-grade-filter.c
	sharpness-filter.c
	gain-filter.cpp
	noise-gate-filter.c
	mask-filter.c
	invert-audio-polarity.c
	compressor-filter.c
	OBSAudioMeter.cpp
	../../../UI/double-slider.cpp)

set(obs-filters_HEADERS
	QGainWidget.h
	OBSAudioMeter.h
	../../../UI/double-slider.hpp)

add_library(obs-filters MODULE
	${obs-filters_HEADERS}
	${obs-filters_SOURCES}
	${obs-filters_config_HEADERS}
	${obs-filters_LIBSPEEXDSP_SOURCES})
target_link_libraries(obs-filters
	libobs
	Qt5::Core
	Qt5::Widgets
	${obs-filters_PLATFORM_DEPS}
	${obs-filters_LIBSPEEXDSP_LIBRARIES})

install_obs_plugin_with_data(obs-filters data)
