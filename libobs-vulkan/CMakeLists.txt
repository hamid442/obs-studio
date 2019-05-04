project(libobs-vulkan)

include_directories(SYSTEM "${CMAKE_SOURCE_DIR}/libobs")

add_definitions(-DLIBOBS_EXPORTS)

set(libobs-vulkan_SOURCES
	vulkan-subsystem.cpp)

set(libobs-vulkan_HEADERS
	vulkan-subsystem.hpp)

add_library(libobs-vulkan MODULE
	${libobs-vulkan_SOURCES}
	${libobs-vulkan_HEADERS})
	
set_target_properties(libobs-vulkan
	PROPERTIES
		OUTPUT_NAME libobs-vulkan
		PREFIX "")
		
target_link_libraries(libobs-vulkan
	libobs)

install_obs_core(libobs-vulkan)