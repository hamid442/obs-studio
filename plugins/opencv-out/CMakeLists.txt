project(opencv-out)

find_package(OpenCV REQUIRED)
#find_library(OPENCV_LIB opencv)
#if(OPENCV_LIB-NOTFOUND)
#if(OPENCV_<MODULE>_FOUND)
#	message(FATAL "OpenCV lib not found")
#endif()

if(WIN32 AND NOT MINGW)
	include_directories(../w32-pthreads)
	set(opencv_PLATFORM_DEPS
		w32-pthreads)
endif()

include_directories(
	${OpenCV_INCLUDE_DIRS}
)

set(opencv-out_HEADERS

)

set(opencv-out_SOURCES
	opencv-out.cpp
)

add_library(opencv-out MODULE
	${opencv-out_HEADERS}
	${opencv-out_SOURCES}
#	${OpenCV_LIBS}
)
	
target_link_libraries(opencv-out
	libobs
	${OpenCV_LIBS}
	${opencv_PLATFORM_DEPS}
)

install_obs_plugin_with_data(opencv-out data)
