project(win-asio)

set(CMAKE_PREFIX_PATH "${QTDIR}")

#include("${CMAKE_SOURCE_DIR}/external/FindLibObs.cmake")
find_package(LibObs REQUIRED)
find_package(Qt5Core REQUIRED)
find_package(Qt5Widgets REQUIRED)

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
	"${CMAKE_SOURCE_DIR}/UI/obs-frontend-api"
	${Qt5Core}
	${Qt5Widgets}
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
	w32-pthreads
	obs-frontend-api
	Qt5::Core
	Qt5::Widgets
	${RTAUDIO_LIBRARY}
)

install_obs_plugin_with_data(win-asio data)
