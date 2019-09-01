project(win-asio)

find_package(LibObs REQUIRED)

include_directories(
	${CMAKE_SOURCE_DIR}/UI/obs-frontend-api
	${JUCE_INCLUDE_DIR}
	${JUCE_INCLUDE_DIR}/modules
)

set(win-asio_HEADERS
	)

set(win-asio_SOURCES
	asio-input.cpp
	)

set(JUCE_LIB optimized ${JUCE_LIBRARY} debug ${JUCE_LIBRARY_DEBUG})

add_library(win-asio MODULE
	${win-asio_SOURCES}
	${win-asio_HEADERS}
)

target_link_libraries(win-asio
	libobs
	obs-frontend-api
	${JUCE_LIB}
)

install_obs_plugin_with_data(win-asio data)
