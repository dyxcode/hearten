cmake_minimum_required(VERSION 2.8)

project(hearten)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS "$ENV{CPPFLAGS} -O3 -g -std=c++17 -Wall -Wno-deprecated -Werror -Wno-unused-function -pthread")

include_directories(${PROJECT_SOURCE_DIR}/src)

add_executable(mytest tests/main.cpp)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

