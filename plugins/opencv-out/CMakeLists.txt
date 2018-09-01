project(opencv-out)

find_package(OPENCV REQUIRED)
find_library(OPENCV_LIB opencv)
if(OPENCV_LIB-NOTFOUND)
	message(FATAL "OpenCV lib not found")
endif()

if(WIN32 AND NOT MINGW)
	include_directories(../w32-pthreads)
	set(opencv_PLATFORM_DEPS
		w32-pthreads)
endif()

include_directories(
	/
	${OPENCV_DIR}
	${OPENCV_DIR}/include
)

set(opencv-out_HEADERS

)

set(opencv-out_SOURCES
	opencv-out.cpp
	${OPENCV}
)

add_library(opencv-out MODULE
	${opencv-out_HEADERS}
	${opencv-out_SOURCES}
	${OPENCV_LIB}
)
	
target_link_libraries(opencv-out
	libobs
	${OPENCV_LIB}
	${opencv_PLATFORM_DEPS}
)

install_obs_plugin_with_data(opencv-out data)
