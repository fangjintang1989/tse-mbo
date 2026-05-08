cmake_minimum_required(VERSION 3.20)

project(tse_mbo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ZLIB REQUIRED)

add_executable(
  tse_mbo
  src/main.cpp
  src/app.cpp
  src/pcap_reader.cpp
  src/network_decoder.cpp
  src/flex_parser.cpp
  src/order_book.cpp
  src/indicative.cpp
)

target_include_directories(tse_mbo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(tse_mbo PRIVATE ZLIB::ZLIB)

if(MSVC)
  target_compile_options(tse_mbo PRIVATE /W4 /permissive-)
else()
  target_compile_options(tse_mbo PRIVATE -Wall -Wextra -Wpedantic)
endif()
