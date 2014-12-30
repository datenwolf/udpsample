set(COMMON_LIBRARIES uvc vpx SDL pthread)

include_directories(
	${CMAKE_SOURCE_DIR}/extern/libuvc/include) 

add_executable(grabcompressandsend
	time.c
	vpx_network.c
	grabcompressandsend.cpp)
target_link_libraries(grabcompressandsend
	${COMMON_LIBRARIES} )

add_executable(receivedecompressandplay
	time.c
	vpx_network.c
	receivedecompressandplay.cpp)
target_link_libraries(receivedecompressandplay
	${COMMON_LIBRARIES} )
