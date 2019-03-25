cmake_minimum_required(VERSION 2.8)

project(mydecoder)

#set(CMAKE_CXX_STANDARD 11)

add_definitions(-D RK_PLAT)

include_directories(/usr/local/include)

set(SOURCE_FILES mydecoder.c)

add_library(mydecoder SHARED ${SOURCE_FILES})

target_link_libraries(mydecoder avcodec avformat avutil swscale rockchip_mpp)