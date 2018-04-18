cmake_minimum_required(VERSION 3.5)

project(obs-midi)

set(CMAKE_PREFIX_PATH "${QTDIR}")
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(LibObs REQUIRED)
find_package(Qt5Core REQUIRED)
find_package(Qt5Widgets REQUIRED)

include_directories(
	/
	SYSTEM "${CMAKE_SOURCE_DIR}/libobs"
	"${CMAKE_SOURCE_DIR}/UI/obs-frontend-api"
	${Qt5Core_INCLUDES}
	${Qt5Widgets_INCLUDES}
)

set(obs-midi_HEADERS
	RtMidi.h
)

set(obs-midi_SOURCES
	obs-midi.cpp
	RtMidi.cpp
)

add_library(obs-midi MODULE
	${obs-midi_HEADERS}
	${obs-midi_SOURCES}
)

target_link_libraries(obs-midi
	libobs
	obs-frontend-api
	Qt5::Core
	Qt5::Widgets
)

install_obs_plugin_with_data(obs-midi data)
#install_external_plugin_with_data(rematrix-filter data)