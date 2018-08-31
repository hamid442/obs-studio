project(opencv-out)

find_package(OPENCV REQUIRED)
find_library(OPENCV_LIB opencv)
if(OPENCV_LIB-NOTFOUND)
	message(FATAL "OpenCV lib not found")
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
)

install_obs_plugin_with_data(opencv-out data)
