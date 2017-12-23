project(win-asio)

if(DISABLE_ASIO)
	message(STATUS "Asio support disabled")
	return()
endif()

find_package(rtaudio)
if(NOT RTAUDIO_ASIO_FOUND AND ENABLE_ASIO)
	message(FATAL_ERROR "asio not found but set as enabled")
elseif(NOT RTAUDIO_ASIO_FOUND)
	message(STATUS "asio not found, disabling asio plugin")
	return()
endif()

include_directories(
	SYSTEM "${CMAKE_SOURCE_DIR}/libobs"
	${RTAUDIO_INCLUDE_DIR}
)
set(win-asio-sdk_HEADERS
	Rtaudio.h
	)

set(win-asio_HEADERS
	)

set(win-asio_SOURCES
	asio-input.cpp
)

add_library(win-asio MODULE
	${win-asio_SOURCES}
	${win-asio_HEADERS}
	${win-asio-sdk_HEADERS}
)
target_link_libraries(win-asio
	libobs
	${RTAUDIO_LIBRARY}
)

install_obs_plugin_with_data(win-asio data)
