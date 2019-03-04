project(ffmpeg-eq)

#set(CMAKE_PREFIX_PATH "${QTDIR}")
#set(CMAKE_INCLUDE_CURRENT_DIR ON)
#set(CMAKE_AUTOMOC ON)
#set(CMAKE_AUTOUIC ON)

#find_package(LibObs REQUIRED)
#find_package(Qt5Core REQUIRED)
#find_package(Qt5Widgets REQUIRED)

#include_directories(
#	SYSTEM "${CMAKE_SOURCE_DIR}/libobs"
#	"${CMAKE_SOURCE_DIR}/UI/obs-frontend-api"
#	${Qt5Core_INCLUDES}
#	${Qt5Widgets_INCLUDES}
#)

find_package(FFmpeg REQUIRED
	COMPONENTS avcodec)
include_directories(${FFMPEG_INCLUDE_DIRS})

if(MSVC)
	set(ffmpeg-eq_PLATFORM_DEPS
		w32-pthreads)
endif()

set(ffmpeg-eq_HEADERS
	fft.h
)

set(ffmpeg-eq_SOURCES
	fft.c
	ffmpeg-eq.cpp
)

add_library(ffmpeg-eq MODULE
	${ffmpeg-eq_SOURCES}
	${ffmpeg-eq_HEADERS}
)
	
target_link_libraries(ffmpeg-eq
	libobs
#	Qt5::Core
#	Qt5::Widgets
	${ffmpeg-eq_PLATFORM_DEPS}
	${FFMPEG_LIBRARIES}
#	obs-frontend-api
)

install_obs_plugin_with_data(ffmpeg-eq data)
