set(COMMON_LIBRARIES vpx pthread)

include_directories(
	${CMAKE_SOURCE_DIR}/extern/libuvc/include
	${CMAKE_SOURCE_DIR}/extern/libyuv/include ) 

add_executable(grabcompressandsend
	time.c
	vpx_network.c
	grabcompressandsend.cpp)
target_link_libraries(grabcompressandsend
	${COMMON_LIBRARIES}
	uvc )

add_executable(receivedecompressandplay
	time.c
	vpx_network.c
	receivedecompressandplay.cpp)
target_link_libraries(receivedecompressandplay
	${COMMON_LIBRARIES} 
	SDL )

add_executable(simple_vpx_encoder
	simple_vpx_encoder.c
	debug_util.c
	ivf.c
	time.c )
target_link_libraries(simple_vpx_encoder
	${COMMON_LIBRARIES} 
	uvc
	yuv )
