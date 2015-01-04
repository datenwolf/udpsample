cmake_minimum_required(VERSION 2.8)
project(RiftSwapper)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")
include(TargetArch)
target_architecture(TARGET_ARCH)

add_subdirectory(extern)

add_subdirectory(src)
