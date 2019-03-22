project(image-source)
project(color-source)
project(obs-slideshow)

if(MSVC)
	set(image-source_PLATFORM_DEPS
		w32-pthreads)
endif()

set(image-source_SOURCES
	image-source.cpp)
set(color-source_SOURCES
	color-source.cpp)
set(obs-slideshow_SOURCES
	obs-slideshow.cpp)

add_library(image-source MODULE
	${image-source_SOURCES})
add_library(color-source MODULE
	${color-source_SOURCES})
add_library(obs-slideshow MODULE
	${obs-slideshow_SOURCES})

target_link_libraries(image-source
	libobs
	Qt5::Widgets
	Qt5::Core
	${image-source_PLATFORM_DEPS})

target_link_libraries(color-source
	libobs
	Qt5::Widgets
	Qt5::Core
	${image-source_PLATFORM_DEPS})

target_link_libraries(obs-slideshow
	libobs
	Qt5::Widgets
	Qt5::Core
	${image-source_PLATFORM_DEPS})

install_obs_plugin_with_data(image-source data)
install_obs_plugin_with_data(color-source data)
install_obs_plugin_with_data(obs-slideshow data)
