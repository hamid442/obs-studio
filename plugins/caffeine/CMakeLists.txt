project(caffeine)

find_package(Caffeine)

if(NOT CAFFEINE_FOUND)
	message(STATUS "caffeine plugin disabled")
	return()
endif()

find_package(Libcurl REQUIRED)
include_directories(${LIBCURL_INCLUDE_DIRS})
add_definitions(${LIBCURL_DEFINITIONS})

if(MSVC)
	set(caffeine_PLATFORM_DEPS
		w32-pthreads)
endif()

include_directories(
	${OBS_JANSSON_INCLUDE_DIRS}
	${CAFFEINE_INCLUDE_DIR})

set(caffeine_SOURCES
	caffeine-api.c
	caffeine-api.h
	caffeine-module.c
	caffeine-service.c
	caffeine-service.h
	caffeine-output.c)

add_library(caffeine MODULE
	${caffeine_SOURCES}
	${caffeine_HEADERS})

target_link_libraries(caffeine
	${CAFFEINE_LIBRARIES}
	${LIBCURL_LIBRARIES}
	${OBS_JANSSON_IMPORT}
	${caffeine_PLATFORM_DEPS}
	libobs)

install_obs_plugin_with_data(caffeine data)

# TODO: this doesn't do the right thing on linux
install_obs_bin(caffeine "" ${CAFFEINE_SHARED})
