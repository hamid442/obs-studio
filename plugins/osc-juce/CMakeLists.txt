project(osc-juce)

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

set(osc-juce_HEADERS
	osc-juce.hpp
	)
set(osc-juce_SOURCES
	osc-juce.cpp
	)

add_library(osc-juce MODULE
	${osc-juce_SOURCES}
	${osc-juce_HEADERS}
)

set(JUCE_LIB optimized ${JUCE_LIBRARY} debug ${JUCE_LIBRARY_DEBUG})

target_link_libraries(osc-juce
	libobs
	obs-frontend-api
	Qt5::Core
	Qt5::Widgets
	${JUCE_LIB}
)

install_obs_plugin_with_data(osc-juce data)
