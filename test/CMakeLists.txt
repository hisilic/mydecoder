cmake_minimum_required(VERSION 2.8)

project(mydecoder_test)

#set(CMAKE_CXX_STANDARD 11)

include_directories(../)

link_directories(../build)

set(SOURCE_FILES mydecoder_test.c)

add_executable(mydecoder_test ${SOURCE_FILES})

target_link_libraries(mydecoder_test mydecoder)
