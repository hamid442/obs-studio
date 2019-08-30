project(obs-vst3)

set(CMAKE_PREFIX_PATH "${QTDIR}")
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(LibObs REQUIRED)
find_package(Qt5Core REQUIRED)
find_package(Qt5Widgets REQUIRED)

include_directories(
	${CMAKE_SOURCE_DIR}/UI/obs-frontend-api
	${JUCE_INCLUDE_DIR}
	${JUCE_INCLUDE_DIR}/modules
)

set(obs-vst3_HEADERS
	obs-vst3.hpp
	)
set(obs-vst3_SOURCES
	obs-vst3.cpp
	)

add_library(obs-vst3 MODULE
	${obs-vst3_SOURCES}
	${obs-vst3_HEADERS}
)

set(JUCE_LIB optimized ${JUCE_LIBRARY} debug ${JUCE_LIBRARY_DEBUG})

target_link_libraries(obs-vst3
	libobs
	obs-frontend-api
	Qt5::Core
	Qt5::Widgets
	${JUCE_LIB}
)

install_obs_plugin_with_data(obs-vst3 data)
