cmake_minimum_required(VERSION 3.27)
project(sort)

set(CMAKE_CXX_STANDARD 17)

add_executable(sort main.cpp)
target_link_libraries(sort pdh)