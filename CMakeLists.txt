cmake_minimum_required(VERSION 3.23)
project(crimson)

set(CMAKE_CXX_STANDARD 20)

add_library(crimson include/crimson/crimson.h include/crimson/tools.h src/crimson.cpp)
target_include_directories(crimson PUBLIC include)
