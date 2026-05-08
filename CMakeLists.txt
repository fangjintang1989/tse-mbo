cmake_minimum_required(VERSION 3.20)

project(tse_mbo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(tse_mbo src/main.cpp)

if(MSVC)
  target_compile_options(tse_mbo PRIVATE /W4 /permissive-)
else()
  target_compile_options(tse_mbo PRIVATE -Wall -Wextra -Wpedantic)
endif()
