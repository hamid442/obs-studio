project(libobs-vulkan)

cmake_minimum_required(VERSION 3.2)

find_package(Vulkan REQUIRED)

if(WIN32)
	add_definitions(-DNOMINMAX -DVK_USE_PLATFORM_WIN32_KHR)
endif()

if(UNIX)
	if (NOT APPLE)
		find_package(X11_XCB REQUIRED)
		find_package(XCB OPTIONAL_COMPONENTS XINPUT)
		if (XCB_XINPUT_FOUND)
			add_definitions(-DVK_USE_PLATFORM_XCB_KHR)
		endif()
	else()
		add_definitions(-DVK_USE_PLATFORM_MACOS_MVK)
	endif()
endif()

include_directories(SYSTEM "${CMAKE_SOURCE_DIR}/libobs")
include_directories("${Vulkan_INCLUDE_DIRS}")
#include_directories(/include)
include_directories(/vulkan_utils)

add_definitions(-DLIBOBS_EXPORTS)

set(libobs-vulkan_SOURCES
	vulkan_utils/utils.cpp
	vulkan-shader.cpp
	vulkan-subsystem.cpp)
	
set(libobs-vulkan_HEADERS
	vulkan.hpp
	vulkan_utils/utils.hpp
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
