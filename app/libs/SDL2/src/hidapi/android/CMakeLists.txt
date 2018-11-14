cmake_minimum_required(VERSION 2.8.11)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Werror")
include_directories(${CMAKE_SOURCE_DIR}/../hidapi/hidapi/hidapi.h)
add_library(hidapi SHARED hid.cpp)

find_library(ANDROID_LOG_LIBRARY log)
target_link_libraries(hidapi ${ANDROID_LOG_LIBRARY})