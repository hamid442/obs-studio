project(obs-vst3)

find_package(LibObs REQUIRED)

include_directories(
	${CMAKE_SOURCE_DIR}/UI/obs-frontend-api
	${JUCE_INCLUDE_DIR}
	${JUCE_INCLUDE_DIR}/modules
)

set(obs-vst3_HEADERS
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
	${JUCE_LIB}
)

install_obs_plugin_with_data(obs-vst3 data)
