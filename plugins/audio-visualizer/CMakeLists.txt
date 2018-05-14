project(audio-visualizer)

set(CMAKE_PREFIX_PATH "${QTDIR}")
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(LibObs REQUIRED)
find_package(Qt5Core REQUIRED)
find_package(Qt5Widgets REQUIRED)

include_directories(
	SYSTEM "${CMAKE_SOURCE_DIR}/libobs"
	"${CMAKE_SOURCE_DIR}/UI/obs-frontend-api"
	${Qt5Core_INCLUDES}
	${Qt5Widgets_INCLUDES}
)

set(audio-visualizer_HEADERS
	audio-visualizer.h
)

set(audio-visualizer_SOURCES
	audio-visualizer.c
)

set(audio-visualizer_QRC
	audio-visualizer.qrc
)

#qt5_add_resources(audio-visualizer_QRC_SOURCES ${audio-visualizer_QRC})

add_library(audio-visualizer MODULE
	${audio-visualizer_SOURCES}
	${audio-visualizer_HEADERS}
	${audio-visualizer_QRC_SOURCES}
)

target_link_libraries(audio-visualizer
	libobs
	obs-frontend-api
	Qt5::Core
	Qt5::Widgets
)

install_obs_plugin_with_data(audio-visualizer data)
