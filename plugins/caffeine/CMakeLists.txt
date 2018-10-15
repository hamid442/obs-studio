project(caffeine)

find_package(Caffeine)

if(NOT CAFFEINE_FOUND)
	message(STATUS "caffeine plugin disabled")
	return()
endif()

find_package(Libcurl REQUIRED)
include_directories(${LIBCURL_INCLUDE_DIRS})
add_definitions(${LIBCURL_DEFINITIONS})

include_directories(
	${OBS_JANSSON_INCLUDE_DIRS}
	${CAFFEINE_INCLUDE_DIR})

set(caffeine_SOURCES
	caffeine-api.h
	caffeine-api.c
	caffeine-module.c
	caffeine-service.c
	caffeine-output.c)

add_library(caffeine MODULE
	${caffeine_SOURCES}
	${caffeine_HEADERS})

target_link_libraries(caffeine
	${CAFFEINE_LIBRARIES}
	${LIBCURL_LIBRARIES}
	${OBS_JANSSON_IMPORT}
	libobs)

#install_obs_plugin_with_data(caffeine data)
install_obs_plugin(caffeine)
install_obs_bin(caffeine "" ${CAFFEINE_SHARED})
