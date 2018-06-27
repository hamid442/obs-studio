project(obs-shader-filter)

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

set(obs-shader-filter_HEADERS
	random-number-generator.h
	SimplexNoise.h
	tinyexpr.h
)

set(obs-shader-filter_SOURCES
	obs-shader-filter.c
	random-number-generator.cpp
	SimplexNoise.cpp
	tinyexpr.c
)

add_library(obs-shader-filter MODULE
	${obs-shader-filter_SOURCES}
	${obs-shader-filter_HEADERS}
)
	
target_link_libraries(obs-shader-filter
	libobs
#	obs-frontend-api
#	Qt5::Core
#	Qt5::Widgets
)

install_obs_plugin_with_data(obs-shader-filter data)