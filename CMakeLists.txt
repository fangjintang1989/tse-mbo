cmake_minimum_required(VERSION 3.20)

project(tse_mbo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ZLIB REQUIRED)

add_library(
  tse_mbo_lib
  libs/app/app.cpp
  libs/ingest/network_decoder.cpp
  libs/ingest/pcap_reader.cpp
  libs/flex/flex_parser.cpp
  libs/book/order_book.cpp
  libs/book/indicative.cpp
)

target_include_directories(tse_mbo_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libs)
target_link_libraries(tse_mbo_lib PUBLIC ZLIB::ZLIB)

add_executable(tse_mbo apps/tse_mbo_cli/main.cpp)
target_link_libraries(tse_mbo PRIVATE tse_mbo_lib)

if(MSVC)
  target_compile_options(tse_mbo_lib PRIVATE /W4 /permissive-)
  target_compile_options(tse_mbo PRIVATE /W4 /permissive-)
else()
  target_compile_options(tse_mbo_lib PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tse_mbo PRIVATE -Wall -Wextra -Wpedantic)
endif()
