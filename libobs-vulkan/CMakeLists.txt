project(libobs-vulkan)

cmake_minimum_required(VERSION 3.2)
find_package(Vulkan REQUIRED)

include_directories(SYSTEM "${CMAKE_SOURCE_DIR}/libobs")
include_directories("${Vulkan_INCLUDE_DIRS}")

add_definitions(-DLIBOBS_EXPORTS)

set(libobs-vulkan_SOURCES
	vulkan-shader.cpp
	vulkan-subsystem.cpp)
	
set(libobs-vulkan_HEADERS
	vulkan-subsystem.hpp)

#[[
set(libobs-vulkan_INCLUDES
vulkan/vk_platform.h
vulkan/vulkan_core.h
vulkan/vulkan_macos.h
vulkan/vulkan_wayland.h
vulkan/vulkan_xlib.h
vulkan/vulkan.h
vulkan/vulkan_fuchsia.h
vulkan/vulkan_metal.h
vulkan/vulkan_win32.h
vulkan/vulkan_xlib_xrandr.h
vulkan/vulkan_android.h
vulkan/vulkan_ios.h
vulkan/vulkan_vi.h
vulkan/vulkan_xcb.h
)
	
source_group(vulkan FILES ${libobs-vulkan_INCLUDES})
]]

add_library(libobs-vulkan MODULE
	${libobs-vulkan_SOURCES}
	${libobs-vulkan_HEADERS})
	
set_target_properties(libobs-vulkan
	PROPERTIES
		OUTPUT_NAME libobs-vulkan
		PREFIX "")
		
target_link_libraries(libobs-vulkan
	libobs
	${Vulkan_LIBRARY})

install_obs_core(libobs-vulkan)
